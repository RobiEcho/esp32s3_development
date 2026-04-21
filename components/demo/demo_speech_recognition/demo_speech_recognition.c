#include "demo.h"

#if SELECTED_DEMO == DEMO_SPEECH_RECOGNITION
#include "speech_recognition.h"
#include "ws2812_led.h"
#include "esp_log.h"

static const char *TAG = "demo_speech";

void demo_speech_recognition(void)
{    
    // 初始化 LED
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);  // 红色：初始化中
    
    // 初始化语音识别模块
    ESP_ERROR_CHECK(speech_recognition_init());
    ESP_ERROR_CHECK(speech_recognition_start());
    
    ws2812_led_set_color(0, 30, 0);  // 绿色：就绪
    ESP_LOGI(TAG, "语音识别已就绪，请说 '小爱同学' 唤醒");
}
#endif
