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
#define MAX_CHUNKS 50                   // 单帧最大分片数量
#define FRAME_TIMEOUT_MS 500            // 帧接收超时时间(毫秒)

// UDP包头
typedef struct __attribute__((packed)) {
    uint32_t frame_id;              // 帧ID
    uint32_t chunk_index;           // 当前分片索引
    uint32_t total_chunks;          // 总分片数
    uint32_t total_size;            // 完整JPEG大小(字节)
    uint32_t chunk_offset;          // 当前分片在完整数据中的偏移
    uint32_t data_len;              // 当前分片数据长度
} udp_header_t;

// UDP包
typedef struct {
    udp_header_t header;            // 包头
    uint8_t *data;                  // 数据指针
} udp_packet_t;

// 帧信息
typedef struct {
    uint32_t frame_id;              // 帧ID
    uint32_t total_chunks;          // 总分片数
    uint32_t total_size;            // 完整JPEG大小
    uint8_t *jpeg_data;             // 预分配的完整JPEG缓冲区
    bool chunk_received[MAX_CHUNKS]; // 分片接收状态（true=已收到）
    uint32_t received_count;        // 已接收分片数
    int64_t start_time;             // 帧开始接收时间(毫秒)
    bool active;                    // 帧是否激活
} frame_info_t;

// JPEG帧(用于队列传递)
typedef struct {
    uint8_t *data;                  // JPEG数据指针
    uint32_t size;                  // JPEG数据大小
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
static QueueHandle_t s_udp_queue = NULL;            // UDP包队列(接收→拼包)
static QueueHandle_t s_assemble_queue = NULL;       // JPEG帧队列(拼包→解码)
static display_buffer_t s_display_buf[2] = {0};     // 双缓冲显示缓冲区
static SemaphoreHandle_t s_frame_ready_sem = NULL;  // 帧就绪信号量
static volatile uint8_t s_displaying_idx = 0xFF;    // 当前显示缓冲区索引(0xFF表示无)
static frame_info_t s_current_frame = {0};          // 当前正在拼装的帧
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;  // 自旋锁保护状态
static video_stats_t s_stats = {0};                 // 统计信息
static jpeg_dec_handle_t s_jpeg_dec = NULL;         // ESP_NEW_JPEG解码器句柄

// UDP接收回调(只做快速拷贝入队)
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

    if (header.data_len != (len - sizeof(udp_header_t)) || 
        header.chunk_index >= header.total_chunks ||
        header.total_chunks > MAX_CHUNKS ||
        header.chunk_offset + header.data_len > header.total_size) {
        return;
    }

    // 分配包结构体
    udp_packet_t *packet = heap_caps_malloc(sizeof(udp_packet_t), MALLOC_CAP_INTERNAL);
    if (!packet) {
        return;
    }

    // 分配数据缓冲区（PSRAM）
    packet->data = heap_caps_malloc(header.data_len, MALLOC_CAP_SPIRAM);
    if (!packet->data) {
        free(packet);
        return;
    }

    packet->header = header;
    memcpy(packet->data, data + sizeof(udp_header_t), header.data_len);

    // 入队
    if (xQueueSend(s_udp_queue, &packet, 0) != pdTRUE) {
        free(packet->data);
        free(packet);
    }
}

// 重置帧状态
// free_data: true=释放jpeg_data内存, false=只清空指针(数据已转移)
static void reset_frame(frame_info_t *frame, bool free_data)
{
    if (frame->jpeg_data) {
        if (free_data) {
            free(frame->jpeg_data);
        }
        frame->jpeg_data = NULL;
    }
    
    for (uint32_t i = 0; i < MAX_CHUNKS; i++) {
        frame->chunk_received[i] = false;
    }
    
    frame->frame_id = 0;
    frame->total_chunks = 0;
    frame->total_size = 0;
    frame->received_count = 0;
    frame->start_time = 0;
    frame->active = false;
}

// 检查帧是否超时
static bool is_frame_timeout(frame_info_t *frame)
{
    if (!frame->active || frame->start_time == 0) {
        return false;
    }
    int64_t now = esp_timer_get_time() / 1000;
    return (now - frame->start_time) > FRAME_TIMEOUT_MS;
}

// 拼包任务(CPU0)
static void assemble_task(void *arg)
{
    ESP_LOGI(TAG, "拼包任务启动");
    
    udp_packet_t *packet = NULL;
    
    while (1) {
        // 从队列取UDP包
        if (xQueueReceive(s_udp_queue, &packet, pdMS_TO_TICKS(10)) != pdTRUE) {
            // 检查超时
            if (is_frame_timeout(&s_current_frame)) {
                s_stats.dropped_frames++;
                ESP_LOGW(TAG, "帧#%lu超时,丢弃", s_current_frame.frame_id);
                reset_frame(&s_current_frame, true);  // 超时需要释放内存
            }
            continue;
        }

        uint32_t fid = packet->header.frame_id;
        uint32_t idx = packet->header.chunk_index;
        uint32_t total = packet->header.total_chunks;
        uint32_t total_size = packet->header.total_size;
        uint32_t offset = packet->header.chunk_offset;
        uint32_t len = packet->header.data_len;

        // 检查是否新帧
        if (!s_current_frame.active || fid != s_current_frame.frame_id) {
            // 丢弃旧帧
            if (s_current_frame.active) {
                s_stats.dropped_frames++;
                ESP_LOGW(TAG, "帧#%lu未完成,切换到帧#%lu", s_current_frame.frame_id, fid);
                reset_frame(&s_current_frame, true);  // 切换帧需要释放旧帧内存
            }
            
            // 初始化新帧 - 预分配完整JPEG缓冲区（PSRAM）
            s_current_frame.jpeg_data = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM);
            if (!s_current_frame.jpeg_data) {
                ESP_LOGE(TAG, "JPEG缓冲区分配失败(需要%lu字节)", total_size);
                free(packet->data);
                free(packet);
                continue;
            }
            
            s_current_frame.frame_id = fid;
            s_current_frame.total_chunks = total;
            s_current_frame.total_size = total_size;
            s_current_frame.received_count = 0;
            s_current_frame.start_time = esp_timer_get_time() / 1000;
            s_current_frame.active = true;
            
            for (uint32_t i = 0; i < MAX_CHUNKS; i++) {
                s_current_frame.chunk_received[i] = false;
            }
            
            // 只在每50帧打印一次
            if (fid % 50 == 1) {
                ESP_LOGI(TAG, "新帧#%lu: %lu字节, %lu片", fid, total_size, total);
            }
        }

        // 检查重复分片
        if (s_current_frame.chunk_received[idx]) {
            ESP_LOGW(TAG, "帧#%lu分片%lu重复,忽略", fid, idx);
            free(packet->data);
            free(packet);
            continue;
        }

        // 直接写入目标位置（零拷贝）
        memcpy(s_current_frame.jpeg_data + offset, packet->data, len);
        s_current_frame.chunk_received[idx] = true;
        s_current_frame.received_count++;

        free(packet->data);  // 释放数据缓冲区
        free(packet);        // 释放包结构体
        packet = NULL;

        // 检查是否收齐
        if (s_current_frame.received_count == s_current_frame.total_chunks) {
            // 检查是否有缺失分片
            bool complete = true;
            for (uint32_t i = 0; i < s_current_frame.total_chunks; i++) {
                if (!s_current_frame.chunk_received[i]) {
                    ESP_LOGE(TAG, "帧#%lu缺少分片%lu,丢弃 (received_count=%lu)", 
                             fid, i, s_current_frame.received_count);
                    complete = false;
                    break;
                }
            }

            if (!complete) {
                s_stats.dropped_frames++;
                reset_frame(&s_current_frame, true);  // 帧不完整需要释放内存
                continue;
            }

            s_stats.total_frames++;
            
            // 每50帧打印一次统计
            if (fid % 50 == 0) {
                float drop_rate = s_stats.total_frames > 0 ? 
                    (float)s_stats.dropped_frames / (s_stats.total_frames + s_stats.dropped_frames) * 100.0f : 0.0f;
                ESP_LOGI(TAG, "帧#%lu | 接收:%lu 丢弃:%lu 解码:%lu | 丢帧率:%.1f%%", 
                         fid, s_stats.total_frames, s_stats.dropped_frames, 
                         s_stats.decoded_frames, drop_rate);
            }

            // 发送到解码队列
            jpeg_frame_t frame = {
                .data = s_current_frame.jpeg_data,
                .size = s_current_frame.total_size
            };

            if (xQueueSend(s_assemble_queue, &frame, 0) != pdTRUE) {
                ESP_LOGW(TAG, "解码队列满,丢帧");
                s_stats.dropped_frames++;
                free(s_current_frame.jpeg_data);  // 队列满需要释放内存
            }

            // 清空当前帧状态（数据已转移给解码任务，不释放内存）
            reset_frame(&s_current_frame, false);
        }
    }
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
        if (xQueueReceive(s_assemble_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // 让出CPU，避免watchdog超时
        vTaskDelay(pdMS_TO_TICKS(1));

        // 查找空闲缓冲区（使用临界区保护）
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
            free(frame.data);
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
            free(frame.data);
            portENTER_CRITICAL(&s_spinlock);
            s_display_buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
            continue;
        }
        
        // 执行解码
        ret = jpeg_dec_process(s_jpeg_dec, &jpeg_io);
        
        // 释放JPEG数据
        free(frame.data);
        
        if (ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "JPEG解码失败: %d", ret);
            s_stats.dropped_frames++;
            portENTER_CRITICAL(&s_spinlock);
            s_display_buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
            continue;
        }
        
        // 检查输出尺寸
        if (header_info.width != 240 || header_info.height != 240) {
            ESP_LOGW(TAG, "解码尺寸异常: 宽度=%d, 高度=%d (期望240x240)", 
                     header_info.width, header_info.height);
        }
        
        s_stats.decoded_frames++;
        
        // 标记为就绪（使用临界区保护）
        portENTER_CRITICAL(&s_spinlock);
        s_display_buf[decode_idx].status = BUF_READY;
        portEXIT_CRITICAL(&s_spinlock);
        
        // 尝试通知显示任务，如果失败则回退状态
        if (xSemaphoreGive(s_frame_ready_sem) != pdTRUE) {
            s_stats.dropped_frames++;
            portENTER_CRITICAL(&s_spinlock);
            s_display_buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// 显示任务(CPU0)
static void display_task(void *arg)
{
    ESP_LOGI(TAG, "显示任务启动");

    while (1) {
        xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY);

        // 查找就绪帧（使用临界区保护）
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
            // 等待 DMA 空闲并设置新的显示索引（使用临界区保护）
            bool dma_ready = false;
            while (!dma_ready) {
                portENTER_CRITICAL(&s_spinlock);
                dma_ready = (s_displaying_idx == 0xFF);
                if (dma_ready) {
                    s_displaying_idx = display_idx;
                }
                portEXIT_CRITICAL(&s_spinlock);
                
                if (!dma_ready) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
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
    s_udp_queue = xQueueCreate(100, sizeof(udp_packet_t*));
    s_assemble_queue = xQueueCreate(5, sizeof(jpeg_frame_t));
    s_frame_ready_sem = xSemaphoreCreateBinary();

    if (!s_udp_queue || !s_assemble_queue || !s_frame_ready_sem) {
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
    xTaskCreatePinnedToCore(assemble_task, "assemble", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(decode_task, "decode", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 6, NULL, 0);

    if (wifi_get_ipv4(ip_str, sizeof(ip_str)) == ESP_OK) {
        ESP_LOGI(TAG, "UDP服务器IP: %s 端口: %d", ip_str, UDP_PORT);
    }
    ESP_LOGI(TAG, "等待UDP视频流...");
}
#endif
