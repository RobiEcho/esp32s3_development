#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ws2812_led.h"
#include "speech_recognition.h"
#include <string.h>

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "系统初始化...");
    // 初始化WS2812
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);// 红色
    
    ESP_ERROR_CHECK(speech_recognition_init());
    ESP_ERROR_CHECK(speech_recognition_start());

    ws2812_led_set_color(0, 30, 0);// LED变绿色表示就绪
    ESP_LOGI(TAG, "语音识别已就绪，请说 '小爱同学' 唤醒");
}
