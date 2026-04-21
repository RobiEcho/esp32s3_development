#include "demo.h"

#if SELECTED_DEMO == DEMO_MQTT_VIDEO
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "video_decode.h"
#include "st7789_lcd.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "demo_mqtt_video";

typedef enum {
    BUF_IDLE = 0,
    BUF_DECODING,
    BUF_READY,
    BUF_DISPLAYING
} buf_status_t;

typedef struct {
    uint16_t *data;                    // RGB565 数据缓冲区
    volatile buf_status_t status;      // 缓冲区状态
} frame_buffer_t;

typedef struct {
    frame_buffer_t buf[2];
} pingpong_buf_t;

static pingpong_buf_t s_pingpong = {0};
static SemaphoreHandle_t s_frame_ready_sem = NULL;
static volatile uint8_t s_displaying_idx = 0xFF;
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t mqtt_app_video_handler(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len)
{
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "接收到无效视频数据");
        return ESP_ERR_INVALID_ARG;
    }
    
    return video_decode_push_data(data, len, offset, total_len);
}

static bool IRAM_ATTR _st7789_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    portENTER_CRITICAL_ISR(&s_spinlock);
    uint8_t idx = s_displaying_idx;
    if (idx < 2) {
        s_pingpong.buf[idx].status = BUF_IDLE;
    }
    s_displaying_idx = 0xFF;
    portEXIT_CRITICAL_ISR(&s_spinlock);
    return false;
}

static void _video_decode_display_task(void *arg)
{
    (void)arg;
    video_frame_info_t frame_info;

    while (1) {
        uint8_t decode_idx = 0xFF;
        portENTER_CRITICAL(&s_spinlock);
        for (uint8_t i = 0; i < 2; i++) {
            if (s_pingpong.buf[i].status == BUF_IDLE) {
                s_pingpong.buf[i].status = BUF_DECODING;
                decode_idx = i;
                break;
            }
        }
        portEXIT_CRITICAL(&s_spinlock);
        
        if (decode_idx == 0xFF) {
            // 没有空闲的显示缓冲区
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        
        esp_err_t ret = video_decode_process(s_pingpong.buf[decode_idx].data, 240 * 240 * 2, &frame_info);
        
        if (ret != ESP_OK) {
            if (ret != ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "视频解码失败");
            }
            portENTER_CRITICAL(&s_spinlock);
            s_pingpong.buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        if (frame_info.width == 240 && frame_info.height == 240) {
            // esp_new_jpeg 直接输出 RGB565_BE 格式，无需字节序转换
            
            // 标记为 READY
            portENTER_CRITICAL(&s_spinlock);
            s_pingpong.buf[decode_idx].status = BUF_READY;
            portEXIT_CRITICAL(&s_spinlock);
            
            if (xSemaphoreGive(s_frame_ready_sem) != pdTRUE) {
                ESP_LOGW(TAG, "显示任务繁忙，跳过此帧");
                portENTER_CRITICAL(&s_spinlock);
                s_pingpong.buf[decode_idx].status = BUF_IDLE;
                portEXIT_CRITICAL(&s_spinlock);
            }
        } else {
            ESP_LOGW(TAG, "视频尺寸不符合要求 (期望: 240x240, 实际: %dx%d)，不显示", frame_info.width, frame_info.height);
            portENTER_CRITICAL(&s_spinlock);
            s_pingpong.buf[decode_idx].status = BUF_IDLE;
            portEXIT_CRITICAL(&s_spinlock);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void _st7789_display_task(void *arg)
{
    (void)arg;

    while (1) {
        xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY);
        
        uint8_t current_display_idx = 0xFF;
        portENTER_CRITICAL(&s_spinlock);
        for (uint8_t i = 0; i < 2; i++) {
            if (s_pingpong.buf[i].status == BUF_READY) {
                s_pingpong.buf[i].status = BUF_DISPLAYING;
                current_display_idx = i;
                break;
            }
        }
        portEXIT_CRITICAL(&s_spinlock);
        
        if (current_display_idx != 0xFF) {
            // 等待 DMA 空闲并设置新的显示索引
            bool dma_ready = false;
            while (!dma_ready) {
                portENTER_CRITICAL(&s_spinlock);
                dma_ready = (s_displaying_idx == 0xFF);
                if (dma_ready) {
                    s_displaying_idx = current_display_idx;
                }
                portEXIT_CRITICAL(&s_spinlock);
                
                if (!dma_ready) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
            st7789_lcd_draw_bitmap(0, 0, 240, 240, s_pingpong.buf[current_display_idx].data);
        }
    }
}

void demo_mqtt_video(void)
{   
    esp_err_t ret;
    
    // 初始化 LED（红色：启动中）
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);
    ESP_LOGI(TAG, "LED: 红色（启动中）");
    
    // 初始化 ST7789 LCD
    ESP_LOGI(TAG, "初始化 ST7789 LCD...");
    ret = st7789_lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 LCD 初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "ST7789 LCD 初始化完成");
    
    // 分配 RGB565 双缓冲区
    s_pingpong.buf[0].data = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    s_pingpong.buf[1].data = heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (s_pingpong.buf[0].data == NULL || s_pingpong.buf[1].data == NULL) {
        ESP_LOGE(TAG, "RGB565 缓冲区分配失败");
        if (s_pingpong.buf[0].data) heap_caps_free(s_pingpong.buf[0].data);
        if (s_pingpong.buf[1].data) heap_caps_free(s_pingpong.buf[1].data);
        return;
    }
    s_pingpong.buf[0].status = BUF_IDLE;
    s_pingpong.buf[1].status = BUF_IDLE;
    ESP_LOGI(TAG, "RGB565 双缓冲区已分配 (每个 %u KB)", (240 * 240 * 2) / 1024);
    
    // 帧就绪信号量
    s_frame_ready_sem = xSemaphoreCreateBinary();
    if (s_frame_ready_sem == NULL) {
        ESP_LOGE(TAG, "帧就绪信号量创建失败");
        heap_caps_free(s_pingpong.buf[0].data);
        heap_caps_free(s_pingpong.buf[1].data);
        return;
    }
    
    // DMA 传输完成回调
    ret = st7789_lcd_register_trans_done_cb(_st7789_trans_done_cb, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DMA 回调注册失败: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_frame_ready_sem);
        heap_caps_free(s_pingpong.buf[0].data);
        heap_caps_free(s_pingpong.buf[1].data);
        return;
    }
    
    // 初始化视频解码模块
    ret = video_decode_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "视频解码初始化失败: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_frame_ready_sem);
        heap_caps_free(s_pingpong.buf[0].data);
        heap_caps_free(s_pingpong.buf[1].data);
        return;
    }
    
    // 启动 WiFi
    ESP_LOGI(TAG, "连接 WiFi...");
    ret = wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 启动失败: %s", esp_err_to_name(ret));
        return;
    }
    
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
    ret = mqtt_app_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 注册 MQTT 数据处理回调
    ret = mqtt_app_register_data_handler(mqtt_app_video_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 回调注册失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 等待 MQTT 连接
    while (!mqtt_app_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // MQTT 连接成功（绿色）
    ws2812_led_set_color(0, 30, 0);
    ESP_LOGI(TAG, "MQTT 已连接");
    ESP_LOGI(TAG, "LED: 绿色（MQTT 已连接）");
    
    // 创建视频解码任务（CPU1，优先级 5）
    BaseType_t task_ret = xTaskCreatePinnedToCore(_video_decode_display_task, "video_decode", 8192, NULL, 5, NULL, 1);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "视频解码任务创建失败");
        return;
    }
    ESP_LOGI(TAG, "视频解码任务已创建（CPU1）");
    
    // 创建 ST7789 显示任务（CPU0，优先级 6）
    task_ret = xTaskCreatePinnedToCore(_st7789_display_task, "st7789_display", 4096, NULL, 6, NULL, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "ST7789 显示任务创建失败");
        return;
    }
    ESP_LOGI(TAG, "ST7789 显示任务已创建（CPU0）");
    
    ESP_LOGI(TAG, "等待接收 JPEG 视频数据...");
}
#endif
