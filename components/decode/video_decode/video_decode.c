#include "video_decode.h"
#include "ring_buf.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "video_decode";

// ================ 数据邮箱结构 ================
/**
 * @brief 存储在 FreeRTOS 队列中的帧元数据
 */
typedef struct {
    uint32_t start_pos;    // 帧在环形缓冲区中的起始物理位置
    uint32_t length;       // 帧数据的总长度
} frame_mbox_t;

#define MAX_FRAME_QUEUE 20 

// ================ 全局资源配置 ================
#define JPEG_BUF_SIZE (250 * 1024)  // 环形缓冲区大小
#define MAX_FRAME_SIZE   (24 * 1024)   // 单帧解码临时缓冲区大小

static ring_buf_t* s_rb = NULL;                 // 环形缓冲区句柄
static QueueHandle_t s_mbox_queue = NULL;       // FreeRTOS 消息队列句柄
static uint8_t *s_decode_temp_buf = NULL;       // 连续的解码临时缓冲区

// 分片重组状态机变量
static uint32_t s_current_frame_total_len = 0;
static size_t s_current_frame_start_pos = 0;
static uint32_t s_current_frame_received = 0;

static bool s_inited = false;
static uint32_t s_dropped_frame_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static jpeg_dec_handle_t s_jpeg_dec = NULL;

// 内部核心解码函数声明
static esp_err_t _decode_jpeg_data(uint8_t *jpeg_data, size_t jpeg_len,
                                    uint16_t *out_rgb565, size_t out_len,
                                    video_frame_info_t *frame_info);

esp_err_t video_decode_init(void)
{
    if (s_inited) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    // 1. 创建环形缓冲区 (使用任务 1 的库)
    s_rb = ring_buf_create(JPEG_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rb) {
        ESP_LOGE(TAG, "RingBuf 创建失败");
        goto _err;
    }

    // 2. 创建 FreeRTOS 队列作为数据邮箱
    s_mbox_queue = xQueueCreate(MAX_FRAME_QUEUE, sizeof(frame_mbox_t));
    if (!s_mbox_queue) {
        ESP_LOGE(TAG, "帧邮箱队列创建失败");
        goto _err;
    }

    // 3. 分配解码临时空间
    s_decode_temp_buf = heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_decode_temp_buf) goto _err;

    // 4. 初始化硬件解码器句柄
    jpeg_dec_config_t config = {
        .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,
        .scale = {.width = 0, .height = 0},
        .clipper = {.width = 0, .height = 0},
        .rotate = JPEG_ROTATE_0D,
        .block_enable = false,
    };
    
    if (jpeg_dec_open(&config, &s_jpeg_dec) != JPEG_ERR_OK) goto _err;

    s_inited = true;
    ESP_LOGI(TAG, "视频解码模块初始化成功 (Queue Mode)");
    return ESP_OK;

_err:
    video_decode_deinit();
    return ESP_FAIL;
}

esp_err_t video_decode_deinit(void)
{
    if (!s_inited) return ESP_OK;

    if (s_jpeg_dec) jpeg_dec_close(s_jpeg_dec);
    if (s_rb) ring_buf_destroy(s_rb);
    if (s_mbox_queue) vQueueDelete(s_mbox_queue);
    if (s_decode_temp_buf) heap_caps_free(s_decode_temp_buf);
    if (s_mutex) vSemaphoreDelete(s_mutex);

    s_inited = false;
    return ESP_OK;
}

esp_err_t video_decode_push_data(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (offset == 0) {
        // 新帧起始，检查队列是否有空间
        if (uxQueueSpacesAvailable(s_mbox_queue) == 0) {
            s_dropped_frame_count++;
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NO_MEM;
        }
        // 记录当前写指针位置作为该帧的 start_pos
        s_current_frame_start_pos = ring_buf_get_write_pos(s_rb);
        s_current_frame_total_len = total_len;
        s_current_frame_received = 0;
    } else if (offset != s_current_frame_received) {
        // 丢包/连续性检测
        s_dropped_frame_count++;
        s_current_frame_received = 0;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    // 写入环形缓冲区 (自动处理跨边界逻辑)
    if (ring_buf_write(s_rb, data, len) != ESP_OK) {
        s_dropped_frame_count++;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    s_current_frame_received += len;

    // 完整帧入队
    if (s_current_frame_received == s_current_frame_total_len) {
        frame_mbox_t mbox = {
            .start_pos = s_current_frame_start_pos,
            .length = s_current_frame_total_len
        };
        if (xQueueSend(s_mbox_queue, &mbox, 0) != pdPASS) {
            s_dropped_frame_count++;
        }
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t video_decode_process(uint16_t *out_rgb565, size_t out_len, video_frame_info_t *frame_info)
{
    if (!s_inited || !out_rgb565) return ESP_ERR_INVALID_ARG;

    frame_mbox_t mbox;
    
    // 1. 从队列获取帧元数据 (非阻塞)
    if (xQueueReceive(s_mbox_queue, &mbox, 0) != pdPASS) {
        return ESP_ERR_NOT_FOUND;
    }

    // 2. 从环形缓冲区提取数据到连续空间 (加锁保护)
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    ring_buf_read(s_rb, s_decode_temp_buf, mbox.start_pos, mbox.length);
    xSemaphoreGive(s_mutex);

    // 3. 调用硬件解码
    esp_err_t ret = _decode_jpeg_data(s_decode_temp_buf, mbox.length, out_rgb565, out_len, frame_info);

    // 4. 释放缓冲区空间：读指针跳转至当前帧末尾
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t next_frame_start = (mbox.start_pos + mbox.length) % JPEG_BUF_SIZE;
    ring_buf_finish_frame(s_rb, next_frame_start);
    xSemaphoreGive(s_mutex);

    return ret;
}

static esp_err_t _decode_jpeg_data(uint8_t *jpeg_data, size_t jpeg_len,
                                    uint16_t *out_rgb565, size_t out_len,
                                    video_frame_info_t *frame_info)
{
    if (!jpeg_data || jpeg_len == 0 || !out_rgb565 || !s_jpeg_dec) {
        return ESP_ERR_INVALID_ARG;
    }

    if (jpeg_len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    jpeg_dec_io_t jpeg_io = {
        .inbuf = jpeg_data,
        .inbuf_len = jpeg_len,
        .outbuf = (uint8_t *)out_rgb565,
    };
    
    jpeg_dec_header_info_t header_info = {0};
    
    // 解析 JPEG 头
    if (jpeg_dec_parse_header(s_jpeg_dec, &jpeg_io, &header_info) != JPEG_ERR_OK) {
        return ESP_FAIL;
    }
    
    // 检查缓冲区是否足够
    size_t required_size = header_info.width * header_info.height * sizeof(uint16_t);
    if (out_len < required_size) {
        return ESP_ERR_NO_MEM;
    }
    
    // 执行解码
    if (jpeg_dec_process(s_jpeg_dec, &jpeg_io) != JPEG_ERR_OK) {
        return ESP_FAIL;
    }

    if (frame_info) {
        frame_info->width = header_info.width;
        frame_info->height = header_info.height;
        frame_info->output_len = required_size;
    }

    return ESP_OK;
}