#include "examples.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "example_led";

void example_led_effects(void)
{
    ESP_LOGI(TAG, "=== LED 效果测试 ===");
    
    // 初始化 LED
    ws2812_led_init();
    
    ESP_LOGI(TAG, "红色");
    ws2812_led_set_color(30, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "绿色");
    ws2812_led_set_color(0, 30, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "蓝色");
    ws2812_led_set_color(0, 0, 30);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "彩虹效果");
    ws2812_led_start_rainbow();
}
