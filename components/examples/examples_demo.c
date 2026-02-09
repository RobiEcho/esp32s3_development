#include "examples.h"
#include "esp_log.h"

static const char *TAG = "examples_demo";

void examples_run_demo(void)
{
#if SELECTED_EXAMPLE == EXAMPLE_AUDIO_LOOPBACK
    ESP_LOGI(TAG, "启动示例 1：音频直通测试");
    example_audio_loopback();
    
#elif SELECTED_EXAMPLE == EXAMPLE_LVGL_DISPLAY
    ESP_LOGI(TAG, "启动示例 2：LVGL 显示测试");
    example_lvgl_display();
    
#elif SELECTED_EXAMPLE == EXAMPLE_LED_EFFECTS
    ESP_LOGI(TAG, "启动示例 3：LED 效果测试");
    example_led_effects();
    
#elif SELECTED_EXAMPLE == EXAMPLE_SPEECH_RECOGNITION
    ESP_LOGI(TAG, "启动示例 4：语音识别测试");
    example_speech_recognition();
    
#elif SELECTED_EXAMPLE == EXAMPLE_MQTT_MPU6050
    ESP_LOGI(TAG, "启动示例 5：MQTT + MPU6050 测试");
    example_wifi_mqtt();
    
#elif SELECTED_EXAMPLE == EXAMPLE_MQTT_IMAGE
    ESP_LOGI(TAG, "启动示例 6：MQTT 图像接收测试");
    example_mqtt_image();
    
#else
    ESP_LOGE(TAG, "错误：未选择有效的示例！");
    ESP_LOGE(TAG, "请在 examples.h 中设置 SELECTED_EXAMPLE 宏");
#endif
}
