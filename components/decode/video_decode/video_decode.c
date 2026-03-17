#include "video_decode.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "video_decode";

#define JPEG_BUFFER_SIZE (250 * 1024)  // 环形缓冲区大小，可存储多帧
#define MAX_FRAME_SIZE   (24 * 1024)   // 单帧最大大小（用于跨边界拷贝）

// 帧元数据结构
typedef struct {
    uint32_t start_pos;    // 帧在缓冲区中的起始位置
    uint32_t length;       // 帧数据长度
} frame_metadata_t;

#define MAX_FRAME_QUEUE 20 // 帧队列最大深度

static uint8_t *s_jpeg_buffer = NULL;
static volatile size_t s_write_pos;     // 环形缓冲区的写位置
static volatile size_t s_read_pos;      // 环形缓冲区的读位置

static bool s_inited = false;
static uint32_t s_dropped_frame_count = 0;  // 丢帧统计
static SemaphoreHandle_t s_mutex = NULL;

// 环形帧队列管理
static frame_metadata_t s_frame_queue[MAX_FRAME_QUEUE];  // 帧元数据队列
static volatile uint8_t s_frame_queue_head;              // 读指针
static volatile uint8_t s_frame_queue_tail;              // 写指针
static volatile uint8_t s_frame_queue_count;             // 当前队列中的帧数

// 当前接收中的帧信息
static uint32_t s_current_frame_total_len;  // 当前帧的总长度
static size_t s_current_frame_start_pos;    // 当前帧在缓冲区中的起始位置
static uint32_t s_current_frame_received;   // 当前帧已接收的字节数（预期的下一个偏移）

// 解码临时缓冲区改为按需动态分配，不再使用静态指针

// ESP_NEW_JPEG解码器句柄
static jpeg_dec_handle_t s_jpeg_dec = NULL;

// 任务通知相关
static TaskHandle_t s_decode_task_handle = NULL;

static esp_err_t _decode_jpeg_data(uint8_t *jpeg_data, size_t jpeg_len,
                                    uint16_t *out_rgb565, size_t out_len,
                                    video_frame_info_t *frame_info);

static inline bool _is_queue_empty(void)
{
    return s_frame_queue_count == 0;
}

static inline bool _is_queue_full(void)
{
    return s_frame_queue_count >= MAX_FRAME_QUEUE;
}

static inline void _enqueue_frame(size_t start_pos, size_t length)
{
    s_frame_queue[s_frame_queue_tail].start_pos = start_pos;
    s_frame_queue[s_frame_queue_tail].length = length;
    s_frame_queue_tail = (s_frame_queue_tail + 1) % MAX_FRAME_QUEUE;
    s_frame_queue_count++;
}

static inline void _dequeue_frame(void)
{
    s_frame_queue_head = (s_frame_queue_head + 1) % MAX_FRAME_QUEUE;
    s_frame_queue_count--;
}

// 安全的丢帧策略 - 丢弃最新帧（当前正在接收的帧）
static inline void _drop_current_frame(void)
{
    // 重置当前帧状态，丢弃正在接收的帧
    s_current_frame_total_len = 0;
    s_current_frame_received = 0;
    // 回退write_pos到当前帧开始位置
    s_write_pos = s_current_frame_start_pos;
    s_dropped_frame_count++;
    ESP_LOGW(TAG, "丢帧 #%u: 丢弃当前接收帧以腾出空间", s_dropped_frame_count);
}

esp_err_t video_decode_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }

    // 分配环形缓冲区
    s_jpeg_buffer = heap_caps_malloc(JPEG_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (s_jpeg_buffer == NULL) {
        ESP_LOGE(TAG, "JPEG 缓冲区分配失败");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 初始化ESP_NEW_JPEG解码器
    jpeg_dec_config_t config = {
        .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,  // RGB565大端输出(ST7789需要)
        .scale = {.width = 0, .height = 0},          // 不缩放
        .clipper = {.width = 0, .height = 0},        // 不裁剪
        .rotate = JPEG_ROTATE_0D,                    // 不旋转
        .block_enable = false,                       // 不使用块模式
    };
    
    jpeg_error_t jpeg_ret = jpeg_dec_open(&config, &s_jpeg_dec);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG解码器初始化失败: %d", jpeg_ret);
        heap_caps_free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_FAIL;
    }

    s_write_pos = 0;
    s_read_pos = 0;
    s_frame_queue_head = 0;
    s_frame_queue_tail = 0;
    s_frame_queue_count = 0;
    s_dropped_frame_count = 0;
    s_current_frame_total_len = 0;
    s_current_frame_start_pos = 0;
    s_current_frame_received = 0;
    s_inited = true;
    
    ESP_LOGI(TAG, "环形缓冲区: %u KB, JPEG解码器已初始化", 
             JPEG_BUFFER_SIZE / 1024);
    return ESP_OK;
}

esp_err_t video_decode_deinit(void)
{
    if (!s_inited) {
        return ESP_OK;
    }

    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }

    s_inited = false;

    if (s_jpeg_dec) {
        jpeg_dec_close(s_jpeg_dec);
        s_jpeg_dec = NULL;
    }

    if (s_jpeg_buffer) {
        heap_caps_free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
    }

    if (s_mutex) {
        xSemaphoreGive(s_mutex);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    ESP_LOGI(TAG, "视频解码器已释放");
    return ESP_OK;
}

esp_err_t video_decode_push_data(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len)
{
    if (!s_inited || !data || len == 0 || total_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (offset == 0) {
        if (total_len > MAX_FRAME_SIZE) {
            ESP_LOGE(TAG, "帧大小超过最大限制 (%u > %u)", total_len, MAX_FRAME_SIZE);
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_SIZE;
        }

        if (_is_queue_full()) {
            // 安全的丢帧策略：丢弃当前正在接收的帧
            _drop_current_frame();
        }

        s_current_frame_total_len = total_len;
        s_current_frame_start_pos = s_write_pos;
        s_current_frame_received = 0;
    } else {
        if (s_current_frame_total_len != total_len) {
            s_dropped_frame_count++;
            ESP_LOGW(TAG, "丢帧 #%u: 帧长度不匹配 (期望: %u, 实际: %u)", 
                     s_dropped_frame_count, s_current_frame_total_len, total_len);
            s_current_frame_total_len = 0;
            s_current_frame_received = 0;
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_ARG;
        }
        
        // 检查分片连续性，检测丢包
        if (offset != s_current_frame_received) {
            s_dropped_frame_count++;
            ESP_LOGW(TAG, "丢帧 #%u: 检测到丢包 (期望偏移: %u, 实际: %u)", 
                     s_dropped_frame_count, s_current_frame_received, offset);
            // 重置当前帧状态，准备接收新帧
            s_current_frame_total_len = 0;
            s_current_frame_received = 0;
            xSemaphoreGive(s_mutex);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // 检查缓冲区剩余空间
    size_t used = (s_write_pos - s_read_pos + JPEG_BUFFER_SIZE) % JPEG_BUFFER_SIZE;
    size_t available = JPEG_BUFFER_SIZE - used - 1;  // 减1避免满空判断冲突
    
    // 如果是新帧开始，需要确保整个帧都能放入缓冲区
    size_t required_space = (offset == 0) ? total_len : len;
    if (available < required_space) {
        s_dropped_frame_count++;
        ESP_LOGW(TAG, "丢帧 #%u: 缓冲区空间不足 (需要: %u, 剩余: %u)", 
                 s_dropped_frame_count, required_space, available);
        // 重置当前帧状态
        s_current_frame_total_len = 0;
        s_current_frame_received = 0;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // 拷贝数据到环形缓冲区
    size_t first_part = JPEG_BUFFER_SIZE - s_write_pos;
    if (len <= first_part) {
        // 不跨边界
        memcpy(s_jpeg_buffer + s_write_pos, data, len);
    } else {
        // 跨边界：分两次拷贝
        memcpy(s_jpeg_buffer + s_write_pos, data, first_part);
        memcpy(s_jpeg_buffer, data + first_part, len - first_part);
    }

    s_write_pos = (s_write_pos + len) % JPEG_BUFFER_SIZE;
    s_current_frame_received += len;

    // 检查当前帧是否接收完整
    if (offset + len == total_len) {
        // 帧接收完整，加入队列
        _enqueue_frame(s_current_frame_start_pos, s_current_frame_total_len);

        // 重置当前帧
        s_current_frame_total_len = 0;
        s_current_frame_received = 0;
        
        // 通知解码任务有新帧可用
        if (s_decode_task_handle) {
            xTaskNotifyGive(s_decode_task_handle);
        }
    } else if (offset + len > total_len) {
        // 数据长度异常，超过了总长度
        s_dropped_frame_count++;
        ESP_LOGW(TAG, "丢帧 #%u: 数据长度异常 (offset=%u, len=%u, total=%u)", 
                 s_dropped_frame_count, offset, len, total_len);
        // 重置当前帧状态
        s_current_frame_total_len = 0;
        s_current_frame_received = 0;
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t video_decode_process(uint16_t *out_rgb565, size_t out_len, 
                                video_frame_info_t *frame_info)
{
    if (!s_inited || !out_rgb565) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 检查队列中是否有完整的帧
    if (_is_queue_empty()) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 从队列头取帧
    frame_metadata_t *frame = &s_frame_queue[s_frame_queue_head];
    size_t frame_start = frame->start_pos;
    size_t frame_len = frame->length;

    // 检查帧大小
    if (frame_len > MAX_FRAME_SIZE) {
        ESP_LOGE(TAG, "帧大小超过最大限制 (%u > %u)", frame_len, MAX_FRAME_SIZE);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *jpeg_data_ptr;
    uint8_t *temp_buffer = NULL;  // 临时缓冲区指针
    
    // 零拷贝优化：判断帧是否跨越环形缓冲区边界
    if (frame_start + frame_len <= JPEG_BUFFER_SIZE) {
        // 不跨界：零拷贝，直接使用环形缓冲区指针
        jpeg_data_ptr = s_jpeg_buffer + frame_start;
        ESP_LOGD(TAG, "零拷贝解码：帧不跨界，直接使用缓冲区指针");
    } else {
        // 跨界：按需分配临时缓冲区，大小等于JPEG帧大小
        temp_buffer = heap_caps_malloc(frame_len, MALLOC_CAP_INTERNAL);
        if (temp_buffer == NULL) {
            ESP_LOGW(TAG, "内部RAM不足，回退到SPIRAM分配临时缓冲区");
            temp_buffer = heap_caps_malloc(frame_len, MALLOC_CAP_SPIRAM);
            if (temp_buffer == NULL) {
                ESP_LOGE(TAG, "临时缓冲区分配失败 (大小: %u)", frame_len);
                xSemaphoreGive(s_mutex);
                return ESP_ERR_NO_MEM;
            }
        }
        
        jpeg_data_ptr = temp_buffer;
        size_t first_part = JPEG_BUFFER_SIZE - frame_start;
        memcpy(jpeg_data_ptr, s_jpeg_buffer + frame_start, first_part);
        memcpy(jpeg_data_ptr + first_part, s_jpeg_buffer, frame_len - first_part);
        ESP_LOGD(TAG, "跨界拷贝：帧跨越边界，动态分配缓冲区 (大小: %u)", frame_len);
    }

    xSemaphoreGive(s_mutex);

    // 解码时不持有锁，所以接收新数据和解码可以同时进行，不会互相阻塞。
    esp_err_t ret = _decode_jpeg_data(jpeg_data_ptr, frame_len, out_rgb565, out_len, frame_info);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 释放帧（无论解码是否成功都要释放）
    _dequeue_frame();

    if (!_is_queue_empty()) {
        // 还有未释放的帧，read_pos移到下一帧的起始
        s_read_pos = s_frame_queue[s_frame_queue_head].start_pos;
    } else {
        // 没有未释放的帧了，整个缓冲区都是空闲的
        s_read_pos = s_write_pos;
    }

    xSemaphoreGive(s_mutex);
    
    // 释放临时缓冲区（如果有分配的话）
    if (temp_buffer) {
        heap_caps_free(temp_buffer);
    }
    
    return ret;
}

static esp_err_t _decode_jpeg_data(uint8_t *jpeg_data, size_t jpeg_len,
                                    uint16_t *out_rgb565, size_t out_len,
                                    video_frame_info_t *frame_info)
{
    if (!jpeg_data || jpeg_len == 0 || !out_rgb565 || !s_jpeg_dec) {
        return ESP_ERR_INVALID_ARG;
    }

    // 基本长度检查
    if (jpeg_len < 4) {
        ESP_LOGE(TAG, "JPEG 数据太短 (长度: %u)", jpeg_len);
        return ESP_ERR_INVALID_ARG;
    }

    // 设置输入输出IO
    jpeg_dec_io_t jpeg_io = {
        .inbuf = jpeg_data,
        .inbuf_len = jpeg_len,
        .outbuf = (uint8_t *)out_rgb565,
    };
    
    jpeg_dec_header_info_t header_info = {0};
    
    // 解析JPEG头
    jpeg_error_t ret = jpeg_dec_parse_header(s_jpeg_dec, &jpeg_io, &header_info);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "解析JPEG头失败: %d", ret);
        return ESP_FAIL;
    }
    
    // 检查输出缓冲区大小
    size_t required_size = header_info.width * header_info.height * sizeof(uint16_t);
    if (out_len < required_size) {
        ESP_LOGE(TAG, "输出缓冲区不足 (需要: %u, 提供: %u)", required_size, out_len);
        return ESP_ERR_NO_MEM;
    }
    
    // 执行解码
    ret = jpeg_dec_process(s_jpeg_dec, &jpeg_io);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG解码失败: %d", ret);
        return ESP_FAIL;
    }

    // 填充帧信息
    if (frame_info) {
        frame_info->width = header_info.width;
        frame_info->height = header_info.height;
        frame_info->output_len = required_size;
    }

    return ESP_OK;
}

// 任务通知相关函数
void video_decode_set_decode_task_handle(TaskHandle_t task_handle)
{
    s_decode_task_handle = task_handle;
    ESP_LOGI(TAG, "解码任务句柄已设置，启用事件驱动模式");
}
