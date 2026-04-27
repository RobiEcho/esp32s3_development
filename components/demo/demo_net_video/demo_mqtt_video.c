#include "demo.h"

#if SELECTED_DEMO == DEMO_MQTT_VIDEO
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "video_decode.h"
#include "pingpong_buf.h"
#include "st7789_lcd.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "demo_mqtt_video";

// ================ 全局句柄 ================
static pingpong_buf_t* s_ppb = NULL;
static SemaphoreHandle_t s_frame_ready_sem = NULL;
static SemaphoreHandle_t s_dma_idle_sem = NULL;
static volatile uint8_t s_current_dma_idx = 0xFF;

// ================ 统计信息 ================
static volatile uint32_t s_frame_decoded = 0;
static volatile uint32_t s_frame_displayed = 0;
static volatile uint32_t s_frame_dropped = 0;

// ================ 回调函数 ================

/**
 * @brief MQTT 视频数据回调
 */
static esp_err_t mqtt_app_video_handler(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len)
{
    if (data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    return video_decode_push_data(data, len, offset, total_len);
}

/**
 * @brief LCD DMA 传输完成中断回调 (ISR Context)
 */
static bool IRAM_ATTR _st7789_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;

    if (s_current_dma_idx != 0xFF) {
        ppb_release(s_ppb, s_current_dma_idx, PPB_CONTEXT_ISR);
        s_current_dma_idx = 0xFF;
    }

    xSemaphoreGiveFromISR(s_dma_idle_sem, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// ================ 任务逻辑 ================

/**
 * @brief 视频解码任务 (CPU1)
 */
static void _video_decode_task(void *arg)
{
    video_frame_info_t frame_info;
    uint8_t idx = 0;

    ESP_LOGI(TAG, "解码任务启动");

    while (1) {
        uint16_t *decode_ptr = (uint16_t*)ppb_get_idle_block(s_ppb, &idx);
        
        if (decode_ptr == NULL) {
            s_frame_dropped++;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        
        esp_err_t ret = video_decode_process(decode_ptr, 240 * 240 * 2, &frame_info);
        
        if (ret == ESP_OK && frame_info.width == 240 && frame_info.height == 240) {
            ppb_set_ready(s_ppb, idx);
            s_frame_decoded++;
            xSemaphoreGive(s_frame_ready_sem);
        } else {
            if (ret != ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "解码失败: %d", ret);
            }
            ppb_release(s_ppb, idx, PPB_CONTEXT_TASK);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/**
 * @brief 显示驱动任务 (CPU0)
 */
static void _st7789_display_task(void *arg)
{
    ESP_LOGI(TAG, "显示任务启动");
    
    while (1) {
        xSemaphoreTake(s_frame_ready_sem, portMAX_DELAY);
        
        uint8_t ready_idx = 0;
        uint16_t *ready_ptr = (uint16_t*)ppb_get_ready_block(s_ppb, &ready_idx);
        
        if (ready_ptr == NULL) {
            continue;
        }
        
        xSemaphoreTake(s_dma_idle_sem, portMAX_DELAY);
        
        ppb_set_displaying(s_ppb, ready_idx);
        s_current_dma_idx = ready_idx;
        s_frame_displayed++;
        
        st7789_lcd_draw_bitmap(0, 0, 240, 240, ready_ptr);
    }
}

/**
 * @brief 统计信息打印任务
 */
static void _stats_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "统计 - 解码:%lu 显示:%lu 丢帧:%lu", 
                 s_frame_decoded, s_frame_displayed, s_frame_dropped);
    }
}

void demo_mqtt_video(void)
{   
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);
    
    ESP_ERROR_CHECK(st7789_lcd_init());
    ESP_ERROR_CHECK(st7789_lcd_register_trans_done_cb(_st7789_trans_done_cb, NULL));
    
    s_ppb = ppb_create(240 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!s_ppb) {
        ESP_LOGE(TAG, "Ping-Pong Buffer 创建失败");
        return;
    }
    
    s_frame_ready_sem = xSemaphoreCreateBinary();
    s_dma_idle_sem = xSemaphoreCreateBinary();
    if (!s_frame_ready_sem || !s_dma_idle_sem) {
        ESP_LOGE(TAG, "信号量创建失败");
        ppb_destroy(s_ppb);
        return;
    }
    xSemaphoreGive(s_dma_idle_sem);
    
    ESP_ERROR_CHECK(video_decode_init());
    
    ESP_ERROR_CHECK(wifi_start());
    while (wifi_get_state() != WIFI_STATE_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
    ws2812_led_set_color(30, 30, 0);
    
    ESP_ERROR_CHECK(mqtt_app_init());
    ESP_ERROR_CHECK(mqtt_app_register_data_handler(mqtt_app_video_handler));
    while (!mqtt_app_is_connected()) vTaskDelay(pdMS_TO_TICKS(500));
    ws2812_led_set_color(0, 30, 0);
    
    xTaskCreatePinnedToCore(_video_decode_task, "vid_dec", 8192, NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(_st7789_display_task, "lcd_dis", 4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(_stats_task, "stats", 2048, NULL, 3, NULL, 0);
    
    ESP_LOGI(TAG, "视频流接收系统已就绪");
}
#endif
