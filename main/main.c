#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "examples.h"
#include "audio_player.h"
#include "max98357a_amp.h"
#include "ws2812_led.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 开发板启动");
    
    // 初始化LED
    ESP_LOGI(TAG, "初始化LED...");
    ws2812_led_init();
    
    // 初始化音频播放器
    ESP_LOGI(TAG, "初始化音频播放器...");
    esp_err_t ret = audio_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "音频播放器初始化失败");
        ws2812_led_set_color(30, 0, 30);  // 紫色表示错误
        return;
    }
    
    // 初始化功放
    ESP_LOGI(TAG, "初始化功放...");
    ret = max98357a_amp_init(6, 512);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "功放初始化失败");
        ws2812_led_set_color(30, 0, 30);  // 紫色表示错误
        return;
    }
    
    ret = max98357a_amp_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "功放使能失败");
        ws2812_led_set_color(30, 0, 30);  // 紫色表示错误
        return;
    }
    
    ESP_LOGI(TAG, "开始循环播放音频测试...");
    
    // 循环播放5个音频
    while (1) {
        for (int i = 0; i < AUDIO_TYPE_MAX; i++) {
            // 红色LED - 等待3秒
            ESP_LOGI(TAG, "等待3秒...");
            ws2812_led_set_color(30, 0, 0);  // 红色
            vTaskDelay(pdMS_TO_TICKS(3000));
            
            // 绿色LED - 播放音频
            ESP_LOGI(TAG, "播放音频 %d", i);
            ws2812_led_set_color(0, 30, 0);  // 绿色
            
            ret = audio_player_play((audio_type_t)i);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "播放音频 %d 失败", i);
                ws2812_led_set_color(30, 0, 30);  // 紫色表示错误
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        
        ESP_LOGI(TAG, "一轮播放完成，重新开始...");
    }
}
