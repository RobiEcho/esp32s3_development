#include "examples.h"

#if SELECTED_EXAMPLE == EXAMPLE_MQTT_IMAGE
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "video_decode.h"
#include "st7789_lcd.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/**
 * 在wifi稳定且高速的情况下：
 * 目前的方案发送端以25帧率的视频帧发送并且网络稳定的情况下，连续发送一段总帧数为725帧的 jpeg 流，丢包包数量为 11，丢帧率 < 0.02%
 * 受限于jpeg解码速度，当发送频率来到30帧，就会出现高频丢包情况，想要稳定不丢丢包，可能需要20帧的视频数据。
 */

static const char *TAG = "example_mqtt_image";

typedef enum {
    BUF_IDLE = 0,
    BUF_DECODING,
    BUF_READY,
    BUF_DISPLAYING
} buf_status_t;

typedef struct {
    uint16_t *buf[2];
    volatile buf_status_t status[2];
} pingpong_buf_t;

static pingpong_buf_t s_pingpong = {0};
static volatile uint8_t s_display_idx = 0;
static SemaphoreHandle_t s_frame_ready_sem = NULL;

static void _swap_rgb565_bytes(uint16_t *buf, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; i++) {
        uint16_t pixel = buf[i];
        buf[i] = ((pixel & 0xFF) << 8) | ((pixel >> 8) & 0xFF);
    }
}

static esp_err_t mqtt_app_image_handler(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len)
{
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "接收到空图像数据");
        return ESP_ERR_INVALID_ARG;
    }
    
    return video_decode_push_data(data, len);
}

static bool IRAM_ATTR _st7789_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    s_pingpong.status[s_display_idx] = BUF_IDLE;
    return false;
}

static void _image_decode_display_task(void *arg)
{
    (void)arg;
    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    video_frame_info_t frame_info;

    while (1) {
        if (video_decode_get_frame(&jpeg_data, &jpeg_len) == ESP_OK) {
            uint8_t decode_idx = 0xFF;
            for (uint8_t i = 0; i < 2; i++) {
                if (s_pingpong.status[i] == BUF_IDLE) {
                    s_pingpong.status[i] = BUF_DECODING;
                    decode_idx = i;
                    break;
                }
            }
            
            if (decode_idx == 0xFF) {
                // 没有空闲缓冲区，释放帧并继续
                video_decode_release_frame(jpeg_len);
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            
            esp_err_t ret = video_decode_process(jpeg_data, jpeg_len, s_pingpong.buf[decode_idx], 240 * 240 * 2, &frame_info);
            
            // 解码完成，立即释放帧空间
            video_decode_release_frame(jpeg_len);
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "图像解码失败: %s", esp_err_to_name(ret));
                s_pingpong.status[decode_idx] = BUF_IDLE;
                continue;
            }

            if (frame_info.width == 240 && frame_info.height == 240) {
                s_pingpong.status[decode_idx] = BUF_READY;
                xSemaphoreGive(s_frame_ready_sem);
            } else {
                ESP_LOGW(TAG, "图像尺寸不符合要求 (期望: 240x240, 实际: %dx%d)，不显示",
                        frame_info.width, frame_info.height);
                s_pingpong.status[decode_idx] = BUF_IDLE;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

static void _st7789_display_task(void *arg)
{
    (void)arg;

    while (1) {
        xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY);
        
        s_display_idx = 0xFF;
        for (uint8_t i = 0; i < 2; i++) {
            if (s_pingpong.status[i] == BUF_READY) {
                s_pingpong.status[i] = BUF_DISPLAYING;
                s_display_idx = i;
                break;
            }
        }
        
        if (s_display_idx != 0xFF) {
            _swap_rgb565_bytes(s_pingpong.buf[s_display_idx], 240 * 240);
            st7789_lcd_draw_bitmap(0, 0, 240, 240, s_pingpong.buf[s_display_idx]);
        }
    }
}

void example_mqtt_image(void)
{   
    // 初始化 LED（红色：启动中）
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);
    ESP_LOGI(TAG, "LED: 红色（启动中）");
    
    // 初始化 ST7789 LCD
    ESP_LOGI(TAG, "初始化 ST7789 LCD...");
    ESP_ERROR_CHECK(st7789_lcd_init());
    ESP_LOGI(TAG, "ST7789 LCD 初始化完成");
    
    // 分配 RGB565 双缓冲区
    s_pingpong.buf[0] = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    s_pingpong.buf[1] = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (s_pingpong.buf[0] == NULL || s_pingpong.buf[1] == NULL) {
        ESP_LOGE(TAG, "RGB565 缓冲区分配失败");
        return;
    }
    s_pingpong.status[0] = BUF_IDLE;
    s_pingpong.status[1] = BUF_IDLE;
    
    // 创建帧就绪信号量（计数信号量，最大值 2）
    s_frame_ready_sem = xSemaphoreCreateCounting(2, 0);
    if (s_frame_ready_sem == NULL) {
        ESP_LOGE(TAG, "帧就绪信号量创建失败");
        return;
    }
    
    // 注册 DMA 传输完成回调
    ESP_ERROR_CHECK(st7789_lcd_register_trans_done_cb(_st7789_trans_done_cb, NULL));
    
    // 初始化视频解码模块
    ESP_ERROR_CHECK(video_decode_init());
    
    // 启动 WiFi
    ESP_LOGI(TAG, "连接 WiFi...");
    ESP_ERROR_CHECK(wifi_start());
    
    // 等待 WiFi 连接
    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // WiFi 连接成功（黄色）
    ws2812_led_set_color(30, 30, 0);
    ESP_LOGI(TAG, "WiFi 已连接");
    ESP_LOGI(TAG, "LED: 黄色（WiFi 已连接）");
    
    // 初始化 MQTT
    ESP_LOGI(TAG, "初始化 MQTT...");
    ESP_ERROR_CHECK(mqtt_app_init());
    
    // 注册 MQTT 数据处理回调
    ESP_ERROR_CHECK(mqtt_app_register_data_handler(mqtt_app_image_handler));
    
    // 等待 MQTT 连接
    while (!mqtt_app_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // MQTT 连接成功（绿色）
    ws2812_led_set_color(0, 30, 0);
    ESP_LOGI(TAG, "MQTT 已连接");
    ESP_LOGI(TAG, "LED: 绿色（MQTT 已连接）");
    
    // 创建图像解码任务（CPU1，优先级 5）
    xTaskCreatePinnedToCore(_image_decode_display_task, "video_decode", 8192, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "视频解码任务已创建（CPU1）");
    
    // 创建 ST7789 显示任务（CPU0，优先级 5）
    xTaskCreatePinnedToCore(_st7789_display_task, "st7789_display", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "ST7789 显示任务已创建（CPU0）");
    
    ESP_LOGI(TAG, "等待接收 JPEG 图像数据...");
}
#endif