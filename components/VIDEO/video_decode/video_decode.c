#include "video_decode.h"
#include "tjpgd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "video_decode";

#define JPEG_BUFFER_SIZE (250 * 1024)  // 环形缓冲区大小，可存储多帧
#define MAX_FRAME_SIZE   (24 * 1024)   // 单帧最大大小（用于跨边界拷贝）
#define TJPGD_WORK_SIZE  (65 * 1024)   // TJpgDec 工作缓冲区大小（JD_FASTDECODE=2 需要 65KB）

// JPEG 标记
#define JPEG_SOI_MARKER 0xFFD8 // Start of Image 
#define JPEG_EOI_MARKER 0xFFD9 // End of Image

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

// 帧队列管理（标准环形队列实现）
static frame_metadata_t s_frame_queue[MAX_FRAME_QUEUE];  // 帧元数据队列
static volatile uint8_t s_frame_queue_head;              // 读指针
static volatile uint8_t s_frame_queue_tail;              // 写指针
static volatile uint8_t s_frame_queue_count;             // 当前队列中的帧数

// 当前接收中的帧信息
static uint32_t s_current_frame_total_len;  // 当前帧的总长度
static size_t s_current_frame_start_pos;    // 当前帧在缓冲区中的起始位置
static uint32_t s_current_frame_received;   // 当前帧已接收的字节数（预期的下一个偏移）

static void *s_tjpgd_work_buffer = NULL;

// 解码临时缓冲区（预分配，避免每帧 malloc/free）
// 注意：不是线程安全的，确保只有一个任务调用 video_decode_process()
static uint8_t *s_decode_temp_buffer = NULL;

typedef struct {
    const uint8_t *jpeg_data;  // JPEG 数据指针
    size_t jpeg_len;           // JPEG 数据长度
    size_t read_offset;        // 当前读取偏移
    uint16_t *out_buffer;      // RGB565 输出缓冲区
    uint16_t out_width;        // 输出宽度
} tjpgd_context_t;

static esp_err_t _decode_jpeg_data(const uint8_t *jpeg_data, size_t jpeg_len,
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

    // 分配 TJpgDec 工作缓冲区
    s_tjpgd_work_buffer = heap_caps_malloc(TJPGD_WORK_SIZE, MALLOC_CAP_SPIRAM);
    if (s_tjpgd_work_buffer == NULL) {
        ESP_LOGE(TAG, "TJpgDec 工作缓冲区分配失败");
        heap_caps_free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 分配解码临时缓冲区
    s_decode_temp_buffer = heap_caps_malloc(MAX_FRAME_SIZE, MALLOC_CAP_SPIRAM);
    if (s_decode_temp_buffer == NULL) {
        ESP_LOGE(TAG, "解码临时缓冲区分配失败");
        heap_caps_free(s_tjpgd_work_buffer);
        s_tjpgd_work_buffer = NULL;
        heap_caps_free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_NO_MEM;
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
    
    ESP_LOGI(TAG, "环形缓冲区: %u KB, 工作缓冲区: %u KB, 解码缓冲区: %u KB", 
             JPEG_BUFFER_SIZE / 1024, TJPGD_WORK_SIZE / 1024, MAX_FRAME_SIZE / 1024);
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

    if (s_jpeg_buffer) {
        heap_caps_free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
    }

    if (s_tjpgd_work_buffer) {
        heap_caps_free(s_tjpgd_work_buffer);
        s_tjpgd_work_buffer = NULL;
    }

    if (s_decode_temp_buffer) {
        heap_caps_free(s_decode_temp_buffer);
        s_decode_temp_buffer = NULL;
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
            s_dropped_frame_count++;
            ESP_LOGW(TAG, "丢帧 #%u: 帧队列已满", s_dropped_frame_count);
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NO_MEM;
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
        ESP_LOGE(TAG, "帧大小超过临时缓冲区 (%u > %u)", frame_len, MAX_FRAME_SIZE);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    if (frame_start + frame_len <= JPEG_BUFFER_SIZE) {
        // 不跨边界
        memcpy(s_decode_temp_buffer, s_jpeg_buffer + frame_start, frame_len);
    } else {
        // 跨边界
        size_t first_part = JPEG_BUFFER_SIZE - frame_start;
        memcpy(s_decode_temp_buffer, s_jpeg_buffer + frame_start, first_part);
        memcpy(s_decode_temp_buffer + first_part, s_jpeg_buffer, frame_len - first_part);
    }

    xSemaphoreGive(s_mutex);

    // // 解码时不持有锁，所以接收新数据和解码可以同时进行，不会互相阻塞。
    esp_err_t ret = _decode_jpeg_data(s_decode_temp_buffer, frame_len, out_rgb565, out_len, frame_info);

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
    return ret;
}

// TJpgDec 输入回调函数（流式读取）
static size_t tjpgd_input_func(JDEC *jd, uint8_t *buff, size_t ndata)
{
    tjpgd_context_t *ctx = (tjpgd_context_t *)jd->device;
    
    if (!ctx) {
        return 0;
    }
    
    // 计算可读取的字节数
    size_t remain = ctx->jpeg_len - ctx->read_offset;
    size_t to_read = (ndata < remain) ? ndata : remain;
    
    if (buff && to_read > 0) {
        // 拷贝数据到 TJpgDec 的缓冲区
        memcpy(buff, ctx->jpeg_data + ctx->read_offset, to_read);
        ctx->read_offset += to_read;
    } else if (!buff) {
        // 跳过数据
        ctx->read_offset += to_read;
    }
    
    return to_read;
}

// TJpgDec 输出回调函数（输出 RGB565 数据）
static int tjpgd_output_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    tjpgd_context_t *ctx = (tjpgd_context_t *)jd->device;
    
    if (!ctx || !bitmap || !rect) {
        return 0;
    }
    
    uint16_t *src = (uint16_t *)bitmap;
    uint16_t *dst = ctx->out_buffer;
    
    // 计算输出位置并拷贝数据
    uint16_t width = rect->right - rect->left + 1;
    uint16_t height = rect->bottom - rect->top + 1;
    
    for (uint16_t y = 0; y < height; y++) {
        uint16_t dst_y = rect->top + y;
        uint16_t *dst_line = dst + dst_y * ctx->out_width + rect->left;
        uint16_t *src_line = src + y * width;
        memcpy(dst_line, src_line, width * sizeof(uint16_t));
    }
    
    return 1;  // 继续解码
}

static esp_err_t _decode_jpeg_data(const uint8_t *jpeg_data, size_t jpeg_len,
                                    uint16_t *out_rgb565, size_t out_len,
                                    video_frame_info_t *frame_info)
{
    if (!jpeg_data || jpeg_len == 0 || !out_rgb565) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_tjpgd_work_buffer) {
        ESP_LOGE(TAG, "TJpgDec 工作缓冲区未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    // 校验 JPEG 帧头尾标记
    if (jpeg_len < 4) {
        ESP_LOGE(TAG, "JPEG 数据太短 (长度: %u)", jpeg_len);
        return ESP_ERR_INVALID_ARG;
    }

    // 检查 SOI 标记 (0xFFD8)
    uint16_t soi_marker = (jpeg_data[0] << 8) | jpeg_data[1];
    if (soi_marker != JPEG_SOI_MARKER) {
        ESP_LOGE(TAG, "JPEG SOI 标记错误 (期望: 0x%04X, 实际: 0x%04X)", 
                 JPEG_SOI_MARKER, soi_marker);
        return ESP_ERR_INVALID_ARG;
    }

    // 检查 EOI 标记 (0xFFD9)
    uint16_t eoi_marker = (jpeg_data[jpeg_len - 2] << 8) | jpeg_data[jpeg_len - 1];
    if (eoi_marker != JPEG_EOI_MARKER) {
        ESP_LOGE(TAG, "JPEG EOI 标记错误 (期望: 0x%04X, 实际: 0x%04X)", 
                 JPEG_EOI_MARKER, eoi_marker);
        return ESP_ERR_INVALID_ARG;
    }

    // 初始化解码上下文
    tjpgd_context_t ctx = {
        .jpeg_data = jpeg_data,
        .jpeg_len = jpeg_len,
        .read_offset = 0,
        .out_buffer = out_rgb565,
        .out_width = 0,  // 将在 jd_prepare 后设置
    };

    JDEC jd;
    JRESULT res;

    // 准备解码器（使用预分配的工作缓冲区）
    res = jd_prepare(&jd, tjpgd_input_func, s_tjpgd_work_buffer, TJPGD_WORK_SIZE, &ctx);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "jd_prepare 失败: %d", res);
        return ESP_FAIL;
    }

    // 设置输出宽度
    ctx.out_width = jd.width;

    // 检查输出缓冲区大小
    size_t required_size = jd.width * jd.height * sizeof(uint16_t);
    if (out_len < required_size) {
        ESP_LOGE(TAG, "输出缓冲区不足 (需要: %u, 提供: %u)", required_size, out_len);
        return ESP_ERR_NO_MEM;
    }

    // 解码（不缩放）
    res = jd_decomp(&jd, tjpgd_output_func, 0);
    if (res != JDR_OK) {
        ESP_LOGE(TAG, "jd_decomp 失败: %d", res);
        return ESP_FAIL;
    }

    // 填充帧信息
    if (frame_info) {
        frame_info->width = jd.width;
        frame_info->height = jd.height;
        frame_info->output_len = required_size;
    }

    return ESP_OK;
}
