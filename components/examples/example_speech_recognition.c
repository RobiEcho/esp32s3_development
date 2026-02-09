#include "examples.h"
#include "speech_recognition.h"
#include "command_handler.h"
#include "ws2812_led.h"
#include "esp_log.h"

static const char *TAG = "example_speech";

// 语音命令回调
static void speech_command_callback(const char *command)
{
    ESP_LOGI(TAG, "收到语音命令: %s", command);
    command_handler_execute(command);
}

void example_speech_recognition(void)
{
    ESP_LOGI(TAG, "=== 语音识别测试 ===");
    
    // 初始化 LED
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);  // 红色：初始化中
    
    // 初始化命令处理器
    ESP_ERROR_CHECK(command_handler_init());
    
    // 初始化语音识别（传入回调函数）
    ESP_ERROR_CHECK(speech_recognition_init(speech_command_callback));
    ESP_ERROR_CHECK(speech_recognition_start());
    
    ws2812_led_set_color(0, 30, 0);  // 绿色：就绪
    ESP_LOGI(TAG, "语音识别已就绪，请说 '小爱同学' 唤醒");
}
