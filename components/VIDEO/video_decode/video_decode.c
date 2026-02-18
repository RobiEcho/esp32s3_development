#include "video_decode.h"
#include "tjpgd.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "video_decode";

#define JPEG_BUFFER_SIZE (200 * 1024)  // 环形缓冲区大小，可存储多帧
#define MAX_FRAME_SIZE   (24 * 1024)   // 单帧最大大小（用于跨边界拷贝）
#define TJPGD_WORK_SIZE  (65 * 1024)   // TJpgDec 工作缓冲区大小（JD_FASTDECODE=2 需要 65KB）

// JPEG 标记
#define JPEG_SOI_MARKER_1 0xFF
#define JPEG_SOI_MARKER_2 0xD8  // Start of Image
#define JPEG_EOI_MARKER_1 0xFF
#define JPEG_EOI_MARKER_2 0xD9  // End of Image

static uint8_t *s_jpeg_buffer = NULL;
static volatile size_t s_write_pos = 0;   // 写入位置
static volatile size_t s_read_pos = 0;    // 读取位置（下次扫描的起点）
static volatile size_t s_buf_data_len = 0;    // 当前缓冲区中的数据量

static bool s_inited = false;
static uint32_t s_dropped_frame_count = 0;
static uint32_t s_wrapped_frame_count = 0;  // 跨边界帧计数
static SemaphoreHandle_t s_mutex = NULL;

// TJpgDec 工作缓冲区（初始化时分配，重复使用）
static void *s_tjpgd_work_buffer = NULL;

// TJpgDec 解码上下文
typedef struct {
    const uint8_t *jpeg_data;  // JPEG 数据指针
    size_t jpeg_len;           // JPEG 数据长度
    size_t read_offset;        // 当前读取偏移
    uint16_t *out_buffer;      // RGB565 输出缓冲区
    uint16_t out_width;        // 输出宽度
} tjpgd_context_t;

esp_err_t video_decode_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    // 分配连续的缓冲区：环形缓冲区 + 镜像区
    size_t total_size = JPEG_BUFFER_SIZE + MAX_FRAME_SIZE;
    s_jpeg_buffer = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
    if (s_jpeg_buffer == NULL) {
        ESP_LOGE(TAG, "JPEG 缓冲区分配失败");
        return ESP_ERR_NO_MEM;
    }

    // 分配 TJpgDec 工作缓冲区
    s_tjpgd_work_buffer = heap_caps_malloc(TJPGD_WORK_SIZE, MALLOC_CAP_SPIRAM);
    if (s_tjpgd_work_buffer == NULL) {
        ESP_LOGE(TAG, "TJpgDec 工作缓冲区分配失败 (需要 %d KB)", TJPGD_WORK_SIZE / 1024);
        heap_caps_free(s_jpeg_buffer);
        return ESP_ERR_NO_MEM;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        heap_caps_free(s_jpeg_buffer);
        heap_caps_free(s_tjpgd_work_buffer);
        return ESP_ERR_NO_MEM;
    }

    s_write_pos = 0;
    s_read_pos = 0;
    s_buf_data_len = 0;
    s_dropped_frame_count = 0;
    s_wrapped_frame_count = 0;

    s_inited = true;
    ESP_LOGI(TAG, "环形缓冲区: %u KB (含 %u KB 镜像区)", 
             total_size / 1024, MAX_FRAME_SIZE / 1024);
    ESP_LOGI(TAG, "TJpgDec 工作缓冲区: %u KB", TJPGD_WORK_SIZE / 1024);
    return ESP_OK;
}

esp_err_t video_decode_push_data(const uint8_t *data, size_t len)
{
    if (!s_inited || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 检查剩余空间是否足够
    size_t free_space = JPEG_BUFFER_SIZE - s_buf_data_len;
    
    if (free_space < len) {
        // 空间不足，丢弃新数据
        s_dropped_frame_count++;
        ESP_LOGW(TAG, "缓冲区空间不足 (需要: %u, 剩余: %u)，丢弃新数据 (总丢帧: %u)", 
                 len, free_space, s_dropped_frame_count);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    // 写入数据到环形缓冲区
    size_t first_part = JPEG_BUFFER_SIZE - s_write_pos;
    if (len <= first_part) {
        // 数据不跨边界
        memcpy(s_jpeg_buffer + s_write_pos, data, len);
    } else {
        // 数据跨边界，分两次拷贝
        memcpy(s_jpeg_buffer + s_write_pos, data, first_part);
        memcpy(s_jpeg_buffer, data + first_part, len - first_part);
        
        // 同时拷贝到镜像区（从 JPEG_BUFFER_SIZE 开始）
        memcpy(s_jpeg_buffer + JPEG_BUFFER_SIZE, data + first_part, len - first_part);
    }
    
    s_write_pos = (s_write_pos + len) % JPEG_BUFFER_SIZE;
    s_buf_data_len += len;

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t video_decode_get_frame(uint8_t **out_data, size_t *out_len)
{
    if (!s_inited || !out_data || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_buf_data_len < 4) {  // 至少需要 SOI(2字节) + EOI(2字节)
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 从 read_pos 开始扫描，查找完整的 JPEG 帧
    size_t scan_pos = s_read_pos;
    size_t scanned = 0;
    size_t max_scan = s_buf_data_len;
    
    bool found_soi = false;
    size_t soi_pos = 0;
    
    while (scanned < max_scan - 1) {
        uint8_t byte1 = s_jpeg_buffer[scan_pos];
        uint8_t byte2 = s_jpeg_buffer[(scan_pos + 1) % JPEG_BUFFER_SIZE];
        
        if (!found_soi) {
            // 查找 SOI (0xFF 0xD8)
            if (byte1 == JPEG_SOI_MARKER_1 && byte2 == JPEG_SOI_MARKER_2) {
                found_soi = true;
                soi_pos = scan_pos;
            }
        } else {
            // 查找 EOI (0xFF 0xD9)
            if (byte1 == JPEG_EOI_MARKER_1 && byte2 == JPEG_EOI_MARKER_2) {
                // 找到完整帧
                size_t eoi_pos = (scan_pos + 1) % JPEG_BUFFER_SIZE;  // EOI 的最后一个字节位置
                size_t frame_len;
                
                // 计算帧长度（从 soi_pos 到 eoi_pos）
                if (eoi_pos >= soi_pos) {
                    // 帧数据连续，不跨边界
                    frame_len = eoi_pos - soi_pos + 1;
                } else {
                    // 帧数据跨边界
                    frame_len = (JPEG_BUFFER_SIZE - soi_pos) + eoi_pos + 1;
                }
                
                // 检查帧大小
                if (frame_len > MAX_FRAME_SIZE) {
                    ESP_LOGE(TAG, "帧大小超过最大限制 (%u > %u)", frame_len, MAX_FRAME_SIZE);
                    // 跳过这个异常帧
                    size_t next_pos = (eoi_pos + 1) % JPEG_BUFFER_SIZE;
                    size_t discard_len;
                    if (next_pos >= s_read_pos) {
                        discard_len = next_pos - s_read_pos;
                    } else {
                        discard_len = (JPEG_BUFFER_SIZE - s_read_pos) + next_pos;
                    }
                    s_read_pos = next_pos;
                    s_buf_data_len -= discard_len;
                    
                    xSemaphoreGive(s_mutex);
                    return ESP_ERR_INVALID_SIZE;
                }
                
                // 直接返回指针（利用镜像区，数据总是连续的）
                *out_data = s_jpeg_buffer + soi_pos;
                *out_len = frame_len;
                
                if (eoi_pos < soi_pos) {
                    // 跨边界帧，统计一下
                    s_wrapped_frame_count++;
                }
                
                // 更新 read_pos，但不减少 data_len（等待 release_frame）
                s_read_pos = (eoi_pos + 1) % JPEG_BUFFER_SIZE;
                
                xSemaphoreGive(s_mutex);
                return ESP_OK;
            }
        }
        
        scan_pos = (scan_pos + 1) % JPEG_BUFFER_SIZE;
        scanned++;
    }

    // 没有找到完整帧，保持 read_pos 不变，等待更多数据
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t video_decode_release_frame(size_t frame_len)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 释放帧占用的空间
    if (frame_len > s_buf_data_len) {
        ESP_LOGE(TAG, "释放帧长度异常 (%u > %u)", frame_len, s_buf_data_len);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    s_buf_data_len -= frame_len;

    xSemaphoreGive(s_mutex);
    return ESP_OK;
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

esp_err_t video_decode_process(const uint8_t *jpeg_data, size_t jpeg_len, 
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
