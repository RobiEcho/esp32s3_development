#include "image_decode.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "image_decode";

#define JPEG_BUFFER_SIZE (200 * 1024)
#define FRAME_QUEUE_SIZE 12

// 数据邮箱
typedef struct {
    uint32_t offset;
    uint32_t len;
} frame_meta_t;

static uint8_t *s_jpeg_buffer = NULL;
static uint32_t s_write_offset = 0;
static uint32_t s_used_space = 0;

static frame_meta_t s_frame_queue[FRAME_QUEUE_SIZE];
static uint8_t s_queue_head = 0;
static uint8_t s_queue_tail = 0;
static uint8_t s_queue_count = 0;


#define MAX_JPEG_FRAME_SIZE (24 * 1024)
static uint8_t *s_decode_temp_buffer = NULL;

static bool s_inited = false;
static uint32_t s_dropped_frame_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t image_decode_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_jpeg_buffer = heap_caps_malloc(JPEG_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (s_jpeg_buffer == NULL) {
        ESP_LOGE(TAG, "JPEG 缓冲区分配失败");
        return ESP_ERR_NO_MEM;
    }

    // 分配解码临时缓冲区
    s_decode_temp_buffer = heap_caps_malloc(MAX_JPEG_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    if (s_decode_temp_buffer == NULL) {
        ESP_LOGE(TAG, "解码临时缓冲区分配失败");
        heap_caps_free(s_jpeg_buffer);
        return ESP_ERR_NO_MEM;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        heap_caps_free(s_jpeg_buffer);
        heap_caps_free(s_decode_temp_buffer);
        return ESP_ERR_NO_MEM;
    }

    s_queue_head = 0;
    s_queue_tail = 0;
    s_queue_count = 0;
    s_write_offset = 0;
    s_used_space = 0;
    s_dropped_frame_count = 0;

    s_inited = true;
    ESP_LOGI(TAG, "初始化完成，分配 %u KB JPEG 缓冲区 + %u KB 解码缓冲区", 
             JPEG_BUFFER_SIZE / 1024, MAX_JPEG_FRAME_SIZE / 1024);
    return ESP_OK;
}

esp_err_t image_decode_push_data(const uint8_t *data, size_t len)
{
    if (!s_inited || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > JPEG_BUFFER_SIZE) {
        ESP_LOGE(TAG, "JPEG 数据过大 (大小: %u, 最大: %u)", len, JPEG_BUFFER_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 如果邮箱队列满，移除最旧的邮箱
    if (s_queue_count >= FRAME_QUEUE_SIZE) {
        frame_meta_t *oldest = &s_frame_queue[s_queue_head];
        s_used_space -= oldest->len;
        s_dropped_frame_count++;
        s_queue_head = (s_queue_head + 1) % FRAME_QUEUE_SIZE;
        s_queue_count--;
        ESP_LOGW(TAG, "队列满，丢弃旧帧 (总丢帧: %u)", s_dropped_frame_count);
    }

    // 检查剩余空间是否足够
    uint32_t free_space = JPEG_BUFFER_SIZE - s_used_space;
    
    if (free_space < len) {
        s_dropped_frame_count++;
        ESP_LOGW(TAG, "缓冲区空间不足 (需要: %u, 剩余: %u)，丢弃新帧 (总丢帧: %u)", 
                 len, free_space, s_dropped_frame_count);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // 写入数据，处理环形回绕
    if (s_write_offset + len <= JPEG_BUFFER_SIZE) {
        // 不需要回绕，直接写入
        memcpy(s_jpeg_buffer + s_write_offset, data, len);
    } else {
        // 需要回绕，分两段写入
        uint32_t first_part = JPEG_BUFFER_SIZE - s_write_offset;  // 写到末尾的部分
        uint32_t second_part = len - first_part;                  // 从头开始的部分
        
        memcpy(s_jpeg_buffer + s_write_offset, data, first_part);
        memcpy(s_jpeg_buffer, data + first_part, second_part);
    }

    // 记录数据邮箱
    s_frame_queue[s_queue_tail].offset = s_write_offset;
    s_frame_queue[s_queue_tail].len = len;
    s_queue_tail = (s_queue_tail + 1) % FRAME_QUEUE_SIZE;
    s_queue_count++;
    s_used_space += len;  // 增加已用空间

    // 更新写入位置（环形）
    s_write_offset = (s_write_offset + len) % JPEG_BUFFER_SIZE;

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t image_decode_get_frame(uint8_t **out_data, size_t *out_len)
{
    if (!s_inited || !out_data || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_queue_count == 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 获取队列头部帧的数据邮箱
    frame_meta_t *meta = &s_frame_queue[s_queue_head];
    
    // 检查帧大小
    if (meta->len > MAX_JPEG_FRAME_SIZE) {
        ESP_LOGE(TAG, "帧大小超过临时缓冲区 (%u > %u)", meta->len, MAX_JPEG_FRAME_SIZE);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    // 拷贝到临时缓冲区
    if (meta->offset + meta->len <= JPEG_BUFFER_SIZE) {
        // 数据连续，直接拷贝
        memcpy(s_decode_temp_buffer, s_jpeg_buffer + meta->offset, meta->len);
    } else {
        // 数据跨越边界，分两段拷贝，处理环形回绕
        uint32_t first_part = JPEG_BUFFER_SIZE - meta->offset;
        uint32_t second_part = meta->len - first_part;
        
        memcpy(s_decode_temp_buffer, s_jpeg_buffer + meta->offset, first_part);
        memcpy(s_decode_temp_buffer + first_part, s_jpeg_buffer, second_part);
    }
    
    *out_data = s_decode_temp_buffer;
    *out_len = meta->len;

    s_queue_head = (s_queue_head + 1) % FRAME_QUEUE_SIZE;
    s_queue_count--;
    s_used_space -= meta->len;  // 减少已用空间

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t image_decode_process(const uint8_t *jpeg_data, size_t jpeg_len, uint16_t *out_rgb565, size_t out_len, image_info_t *img_info)
{
    if (!jpeg_data || jpeg_len == 0 || !out_rgb565) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpeg_data,
        .indata_size = jpeg_len,
        .outbuf = (uint8_t *)out_rgb565,
        .outbuf_size = out_len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
    };

    esp_jpeg_image_output_t esp_img_info = {0};

    esp_err_t ret = esp_jpeg_decode(&cfg, &esp_img_info);
    if (ret != ESP_OK) {
        return ret;
    }

    if (img_info) {
        img_info->width = esp_img_info.width;
        img_info->height = esp_img_info.height;
        img_info->output_len = esp_img_info.output_len;
    }

    return ESP_OK;
}
