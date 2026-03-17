#include "examples.h"

#if SELECTED_EXAMPLE == EXAMPLE_UDP_VIDEO
#include "wifi_manager.h"
#include "udp_server.h"
#include "st7789_lcd.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "udp_video";

#define UDP_PORT 8888                   // UDP服务器监听端口
#define MAX_CHUNKS 30                   // 单帧最大分片数量
#define FRAME_TIMEOUT_MS 100            // 帧接收超时时间(毫秒)

// 内存池配置
#define JPEG_FRAME_POOL_SIZE 5          // JPEG帧缓冲区池大小
#define MAX_JPEG_FRAME_SIZE (24 * 1024) // 单帧JPEG最大大小(24KB)

// UDP包头
typedef struct __attribute__((packed)) {
    uint32_t frame_id;              // 帧ID
    uint32_t chunk_index;           // 当前分片索引
    uint32_t total_chunks;          // 总分片数
    uint32_t total_size;            // 完整JPEG大小(字节)
    uint32_t chunk_offset;          // 当前分片在完整数据中的偏移
    uint32_t data_len;              // 当前分片数据长度
} udp_header_t;

// JPEG帧缓冲区（缓冲区+状态合并）
typedef struct {
    uint8_t *buffer;                         // JPEG缓冲区
    uint32_t frame_id;                       // 帧ID
    uint32_t total_chunks;                   // 总分片数
    uint32_t total_size;                     // 完整JPEG大小
    bool chunk_received[MAX_CHUNKS];         // 分片接收状态
    uint32_t received_count;                 // 已接收分片数
    int64_t start_time;                      // 开始接收时间(毫秒)
    bool active;                             // 是否激活
} jpeg_frame_buffer_t;

// JPEG帧缓冲区池
typedef struct {
    jpeg_frame_buffer_t frames[JPEG_FRAME_POOL_SIZE];  // 帧缓冲区数组
    uint8_t next_alloc_idx;                             // 轮转分配索引
    SemaphoreHandle_t mutex;                            // 互斥锁
} jpeg_frame_pool_t;

// JPEG帧(用于队列传递)
typedef struct {
    uint8_t *data;                  // JPEG数据指针
    uint32_t size;                  // JPEG数据大小
    uint8_t pool_idx;               // 缓冲区池索引
} jpeg_frame_t;

// 显示缓冲区状态
typedef enum {
    BUF_IDLE = 0,                   // 空闲
    BUF_DECODING,                   // 解码中
    BUF_READY,                      // 就绪待显示
    BUF_DISPLAYING                  // 显示中
} buf_status_t;

// 显示缓冲区
typedef struct {
    uint16_t *data;                 // RGB565像素数据(240x240x2字节)
    volatile buf_status_t status;   // 缓冲区状态
} display_buffer_t;

// 统计信息
typedef struct {
    uint32_t total_frames;          // 总接收帧数
    uint32_t dropped_frames;        // 丢弃的帧数
    uint32_t decoded_frames;        // 成功解码的帧
} video_stats_t;

// 全局变量
static jpeg_frame_pool_t s_jpeg_pool = {0};         // JPEG帧缓冲区池
static QueueHandle_t s_decode_queue = NULL;         // 解码队列
static display_buffer_t s_display_buf[2] = {0};     // 双缓冲显示缓冲区
static SemaphoreHandle_t s_frame_ready_sem = NULL;  // 帧就绪信号量
static volatile uint8_t s_displaying_idx = 0xFF;    // 当前显示缓冲区索引(0xFF表示无)
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;  // 自旋锁保护状态
static video_stats_t s_stats = {0};                 // 统计信息
static jpeg_dec_handle_t s_jpeg_dec = NULL;         // ESP_NEW_JPEG解码器句柄

// JPEG帧缓冲区池初始化
static esp_err_t jpeg_frame_pool_init(void)
{
    for (int i = 0; i < JPEG_FRAME_POOL_SIZE; i++) {
        s_jpeg_pool.frames[i].buffer = heap_caps_malloc(MAX_JPEG_FRAME_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_jpeg_pool.frames[i].buffer) {
            ESP_LOGE(TAG, "JPEG缓冲区%d分配失败", i);
            // 释放已分配的
            for (int j = 0; j < i; j++) {
                heap_caps_free(s_jpeg_pool.frames[j].buffer);
            }
            return ESP_ERR_NO_MEM;
        }
        // 初始化帧状态
        s_jpeg_pool.frames[i].active = false;
        s_jpeg_pool.frames[i].frame_id = 0;
    }
    
    s_jpeg_pool.next_alloc_idx = 0;
    
    s_jpeg_pool.mutex = xSemaphoreCreateMutex();
    if (!s_jpeg_pool.mutex) {
        for (int i = 0; i < JPEG_FRAME_POOL_SIZE; i++) {
            heap_caps_free(s_jpeg_pool.frames[i].buffer);
        }
        ESP_LOGE(TAG, "JPEG池互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "JPEG帧缓冲区池初始化完成: %d个缓冲区, 每个%dKB", JPEG_FRAME_POOL_SIZE, MAX_JPEG_FRAME_SIZE / 1024);
    return ESP_OK;
}

// 查找帧ID对应的缓冲区索引（-1表示未找到）
static int8_t find_frame_buffer(uint32_t frame_id)
{
    for (int i = 0; i < JPEG_FRAME_POOL_SIZE; i++) {
        if (s_jpeg_pool.frames[i].active && s_jpeg_pool.frames[i].frame_id == frame_id) {
            return i;
        }
    }
    return -1;
}

// 分配新的缓冲区（轮转分配）
static int8_t alloc_frame_buffer(void)
{
    uint8_t start_idx = s_jpeg_pool.next_alloc_idx;
    uint8_t i = start_idx;
    
    do {
        if (!s_jpeg_pool.frames[i].active) {
            s_jpeg_pool.next_alloc_idx = (i + 1) % JPEG_FRAME_POOL_SIZE;
            return i;
        }
        i = (i + 1) % JPEG_FRAME_POOL_SIZE;
    } while (i != start_idx);
    
    return -1;  // 池已满
}

// 初始化帧状态
static void init_frame_state(uint8_t idx, uint32_t frame_id, uint32_t total_chunks, uint32_t total_size)
{
    jpeg_frame_buffer_t *frame = &s_jpeg_pool.frames[idx];
    frame->frame_id = frame_id;
    frame->total_chunks = total_chunks;
    frame->total_size = total_size;
    frame->received_count = 0;
    frame->start_time = esp_timer_get_time() / 1000;
    frame->active = true;
    
    for (uint32_t i = 0; i < MAX_CHUNKS; i++) {
        frame->chunk_received[i] = false;
    }
}

// 释放帧缓冲区
static inline void release_frame_buffer(uint8_t idx)
{
    if (idx >= JPEG_FRAME_POOL_SIZE) return;
    s_jpeg_pool.frames[idx].active = false;
}

// 检查并清理超时帧
static void check_and_cleanup_timeout_frames(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    
    for (int i = 0; i < JPEG_FRAME_POOL_SIZE; i++) {
        if (s_jpeg_pool.frames[i].active) {
            if ((now - s_jpeg_pool.frames[i].start_time) > FRAME_TIMEOUT_MS) {
                ESP_LOGW(TAG, "帧#%lu超时,释放缓冲区%d", s_jpeg_pool.frames[i].frame_id, i);
                release_frame_buffer(i);
                s_stats.dropped_frames++;
            }
        }
    }
}

// UDP接收回调
static void udp_recv_handler(const uint8_t *data, size_t len, const char *src_addr, uint16_t src_port)
{
    if (len < sizeof(udp_header_t)) {
        return;
    }

    // 解析包头
    udp_header_t header;
    memcpy(&header, data, sizeof(udp_header_t));
    header.frame_id = ntohl(header.frame_id);
    header.chunk_index = ntohl(header.chunk_index);
    header.total_chunks = ntohl(header.total_chunks);
    header.total_size = ntohl(header.total_size);
    header.chunk_offset = ntohl(header.chunk_offset);
    header.data_len = ntohl(header.data_len);

    // 基本校验
    if (header.data_len != (len - sizeof(udp_header_t)) || 
        header.chunk_index >= header.total_chunks ||
        header.total_chunks > MAX_CHUNKS ||
        header.chunk_offset + header.data_len > header.total_size ||
        header.total_size > MAX_JPEG_FRAME_SIZE) {
        return;
    }

    // 加锁保护缓冲区池
    xSemaphoreTake(s_jpeg_pool.mutex, portMAX_DELAY);

    // 先清理超时帧
    check_and_cleanup_timeout_frames();

    // 查找这个帧是否已经在拼装
    int8_t buf_idx = find_frame_buffer(header.frame_id);
    
    if (buf_idx < 0) {
        // 新帧，分配缓冲区
        buf_idx = alloc_frame_buffer();
        if (buf_idx < 0) {
            // 没有空闲缓冲区，丢弃这个包
            ESP_LOGW(TAG, "JPEG缓冲区池已满,丢弃帧#%lu的包", header.frame_id);
            xSemaphoreGive(s_jpeg_pool.mutex);
            return;
        }
        
        // 初始化新帧状态
        init_frame_state(buf_idx, header.frame_id, header.total_chunks, header.total_size);
        
        // 只在每50帧打印一次
        if (header.frame_id % 50 == 1) {
            ESP_LOGI(TAG, "新帧#%lu: %lu字节, %lu片, 缓冲区%d", 
                     header.frame_id, header.total_size, header.total_chunks, buf_idx);
        }
    }

    jpeg_frame_buffer_t *frame = &s_jpeg_pool.frames[buf_idx];

    // 检查重复分片
    if (frame->chunk_received[header.chunk_index]) {
        xSemaphoreGive(s_jpeg_pool.mutex);
        return;
    }

    // 直接写入目标位置
    memcpy(frame->buffer + header.chunk_offset, 
           data + sizeof(udp_header_t), 
           header.data_len);
    frame->chunk_received[header.chunk_index] = true;
    frame->received_count++;

    // 检查是否收齐
    if (frame->received_count == frame->total_chunks) {
        // 检查是否有缺失分片
        bool complete = true;
        for (uint32_t i = 0; i < frame->total_chunks; i++) {
            if (!frame->chunk_received[i]) {
                ESP_LOGE(TAG, "帧#%lu缺少分片%lu,丢弃", header.frame_id, i);
                complete = false;
                break;
            }
        }

        if (!complete) {
            s_stats.dropped_frames++;
            release_frame_buffer(buf_idx);
            xSemaphoreGive(s_jpeg_pool.mutex);
            return;
        }

        s_stats.total_frames++;
        
        // 每50帧打印一次统计
        if (header.frame_id % 50 == 0) {
            float drop_rate = s_stats.total_frames > 0 ? 
                (float)s_stats.dropped_frames / (s_stats.total_frames + s_stats.dropped_frames) * 100.0f : 0.0f;
            ESP_LOGI(TAG, "帧#%lu | 接收:%lu 丢弃:%lu 解码:%lu | 丢帧率:%.1f%%", 
                     header.frame_id, s_stats.total_frames, s_stats.dropped_frames, 
                     s_stats.decoded_frames, drop_rate);
        }

        // 发送到解码队列
        jpeg_frame_t frame_data = {
            .data = frame->buffer,
            .size = frame->total_size,
            .pool_idx = buf_idx
        };

        if (xQueueSend(s_decode_queue, &frame_data, 0) != pdTRUE) {
            ESP_LOGW(TAG, "解码队列满,丢帧#%lu", header.frame_id);
            s_stats.dropped_frames++;
            release_frame_buffer(buf_idx);
        } else {
            // 成功入队，标记为非激活（但不释放，等解码完成后释放）
            frame->active = false;
        }
    }

    xSemaphoreGive(s_jpeg_pool.mutex);
}

// DMA完成回调
static bool IRAM_ATTR st7789_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    portENTER_CRITICAL_ISR(&s_spinlock);
    uint8_t idx = s_displaying_idx;
    if (idx < 2) {
        s_display_buf[idx].status = BUF_IDLE;
    }
    s_displaying_idx = 0xFF;
    portEXIT_CRITICAL_ISR(&s_spinlock);
    return false;
}

// 解码任务(CPU1)
static void decode_task(void *arg)
{
    ESP_LOGI(TAG, "解码任务启动");

    jpeg_frame_t frame;

    while (1) {
        if (xQueueReceive(s_decode_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint8_t decode_idx = 0xFF;
        portENTER_CRITICAL(&s_spinlock);
        for (uint8_t i = 0; i < 2; i++) {
            if (s_display_buf[i].status == BUF_IDLE) {
                s_display_buf[i].status = BUF_DECODING;
                decode_idx = i;
                break;
            }
        }
        portEXIT_CRITICAL(&s_spinlock);

        if (decode_idx == 0xFF) {
            ESP_LOGW(TAG, "无空闲显示缓冲区,丢帧");
            s_stats.dropped_frames++;
            // 释放JPEG缓冲区
            xSemaphoreTake(s_jpeg_pool.mutex, portMAX_DELAY);
            release_frame_buffer(frame.pool_idx);
            xSemaphoreGive(s_jpeg_pool.mutex);
            continue;
        }

        // 设置输入输出IO
        jpeg_dec_io_t jpeg_io = {
            .inbuf = frame.data,
            .inbuf_len = frame.size,
            .outbuf = (uint8_t *)s_display_buf[decode_idx].data,
        };
        
        jpeg_dec_header_info_t header_info = {0};
        
        // 解析JPEG头
        jpeg_error_t ret = jpeg_dec_parse_header(s_jpeg_dec, &jpeg_io, &header_info);
        if (ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "解析JPEG头失败: %d", ret);
            s_stats.dropped_frames++;
            xSemaphoreTake(s_jpeg_pool.mutex, portMAX_DELAY);
            release_frame_buffer(frame.pool_idx);
            xSemaphoreGive(s_jpeg_pool.mutex);
            portENTER_CRITICAL(&s_spinlock);
            s_display_buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
            continue;
        }
        
        // 执行解码
        ret = jpeg_dec_process(s_jpeg_dec, &jpeg_io);
        
        // 释放JPEG缓冲区
        xSemaphoreTake(s_jpeg_pool.mutex, portMAX_DELAY);
        release_frame_buffer(frame.pool_idx);
        xSemaphoreGive(s_jpeg_pool.mutex);
        
        if (ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "JPEG解码失败: %d", ret);
            s_stats.dropped_frames++;
            portENTER_CRITICAL(&s_spinlock);
            s_display_buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
            continue;
        }

        if (header_info.width != 240 || header_info.height != 240) {
            ESP_LOGW(TAG, "解码尺寸异常: 宽度=%d, 高度=%d (期望240x240)", 
                     header_info.width, header_info.height);
        }
        
        s_stats.decoded_frames++;

        portENTER_CRITICAL(&s_spinlock);
        s_display_buf[decode_idx].status = BUF_READY;
        portEXIT_CRITICAL(&s_spinlock);
        
        // 通知显示任务
        if (xSemaphoreGive(s_frame_ready_sem) != pdTRUE) {
            s_stats.dropped_frames++;
            portENTER_CRITICAL(&s_spinlock);
            s_display_buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
        }
    }
}

// 显示任务(CPU0)
static void display_task(void *arg)
{
    ESP_LOGI(TAG, "显示任务启动");

    while (1) {
        xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY);

        // 查找就绪缓冲区
        uint8_t display_idx = 0xFF;
        portENTER_CRITICAL(&s_spinlock);
        for (uint8_t i = 0; i < 2; i++) {
            if (s_display_buf[i].status == BUF_READY) {
                s_display_buf[i].status = BUF_DISPLAYING;
                display_idx = i;
                break;
            }
        }
        portEXIT_CRITICAL(&s_spinlock);

        if (display_idx != 0xFF) {
            // 直接提交到SPI队列，非阻塞
            portENTER_CRITICAL(&s_spinlock);
            s_displaying_idx = display_idx;
            portEXIT_CRITICAL(&s_spinlock);
            
            st7789_lcd_draw_bitmap(0, 0, 240, 240, s_display_buf[display_idx].data);
        }
    }
}

void example_udp_video(void)
{
    esp_err_t ret;

    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);

    ret = st7789_lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD初始化失败");
        return;
    }

    // 初始化JPEG帧缓冲区池
    ret = jpeg_frame_pool_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG帧池初始化失败");
        return;
    }

    // 分配显示缓冲区
    s_display_buf[0].data = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    s_display_buf[1].data = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s_display_buf[0].data || !s_display_buf[1].data) {
        ESP_LOGE(TAG, "显示缓冲区分配失败");
        return;
    }
    s_display_buf[0].status = BUF_IDLE;
    s_display_buf[1].status = BUF_IDLE;

    // 创建队列
    s_decode_queue = xQueueCreate(5, sizeof(jpeg_frame_t));
    s_frame_ready_sem = xSemaphoreCreateBinary();

    if (!s_decode_queue || !s_frame_ready_sem) {
        ESP_LOGE(TAG, "队列创建失败");
        return;
    }

    // 初始化ESP_NEW_JPEG解码器
    jpeg_dec_config_t config = {
        .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,   // RGB565大端输出(ST7789需要)
        .scale = {.width = 0, .height = 0},           // 不缩放
        .clipper = {.width = 0, .height = 0},         // 不裁剪
        .rotate = JPEG_ROTATE_0D,                     // 不旋转
        .block_enable = false,                        // 不使用块模式
    };
    
    jpeg_error_t jpeg_ret = jpeg_dec_open(&config, &s_jpeg_dec);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG解码器初始化失败: %d", jpeg_ret);
        return;
    }
    ESP_LOGI(TAG, "JPEG解码器初始化完成");

    ret = st7789_lcd_register_trans_done_cb(st7789_trans_done_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DMA回调注册失败");
        return;
    }

    ret = wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi启动失败");
        return;
    }

    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ws2812_led_set_color(30, 30, 0);

    char ip_str[16];
    if (wifi_get_ipv4(ip_str, sizeof(ip_str)) == ESP_OK) {
        ESP_LOGI(TAG, "IP: %s", ip_str);
    }

    ret = udp_server_start(UDP_PORT, udp_recv_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDP服务器启动失败");
        return;
    }

    ws2812_led_set_color(0, 30, 0);

    // 创建任务
    xTaskCreatePinnedToCore(decode_task, "decode", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 6, NULL, 0);

    if (wifi_get_ipv4(ip_str, sizeof(ip_str)) == ESP_OK) {
        ESP_LOGI(TAG, "UDP服务器IP: %s 端口: %d", ip_str, UDP_PORT);
    }
    ESP_LOGI(TAG, "等待UDP视频流...");
}
#endif
