#include "demo.h"

#ifdef SELECTED_DEMO
#include "esp_log.h"

static const char *TAG = "demo";

void demo_run(void)
{
#if SELECTED_DEMO == DEMO_AUDIO_LOOPBACK
    ESP_LOGI(TAG, "启动示例 1：音频直通测试");
    demo_audio_loopback();
    
#elif SELECTED_DEMO == DEMO_LVGL_DISPLAY
    ESP_LOGI(TAG, "启动示例 2：LVGL 显示测试");
    demo_lvgl_display();
    
#elif SELECTED_DEMO == DEMO_LED_EFFECTS
    ESP_LOGI(TAG, "启动示例 3：LED 效果测试");
    demo_led_effects();
    
#elif SELECTED_DEMO == DEMO_SPEECH_RECOGNITION
    ESP_LOGI(TAG, "启动示例 4：语音识别测试");
    demo_speech_recognition();
    
#elif SELECTED_DEMO == DEMO_MQTT_MPU6050
    ESP_LOGI(TAG, "启动示例 5：MQTT + MPU6050 测试");
    demo_wifi_mqtt();
    
#elif SELECTED_DEMO == DEMO_MQTT_VIDEO
    ESP_LOGI(TAG, "启动示例 6：MQTT 视频接收测试");
    demo_mqtt_video();
    
#elif SELECTED_DEMO == DEMO_UDP_VIDEO
    ESP_LOGI(TAG, "启动示例 7：UDP 视频接收测试");
    demo_udp_video();
    
#else
    ESP_LOGE(TAG, "错误：未选择有效的示例！");
    ESP_LOGE(TAG, "请在 demo.h 中设置 SELECTED_DEMO 宏");
#endif
}
#endif
