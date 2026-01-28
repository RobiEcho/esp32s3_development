#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ws2812_led.h"
#include "st7789_lcd.h"
#include "wifi_manager.h"
#include "mymqtt.h"

static const char *TAG = "main";

// MQTT图像接收回调 - 将接收到的图像绘制到ST7789
static void mqtt_image_callback(const uint16_t *image_data)
{
    if (image_data == NULL) {
        ESP_LOGW(TAG, "接收到空图像数据");
        return;
    }
    
    ESP_LOGI(TAG, "接收到完整图像帧，开始绘制...");
    
    // 绘制240x240的RGB565图像到ST7789
    // image_data是240*240*2字节的RGB565数据
    st7789_lcd_draw_bitmap(0, 0, 240, 240, (void *)image_data);
    
    ESP_LOGI(TAG, "图像绘制完成");
}

void app_main(void)
{
    // 初始化WS2812，设置为红色（30/255亮度）
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);  // 红色
    ESP_LOGI(TAG, "LED: 红色 - 系统启动中...");
    
    // 初始化ST7789 LCD
    ESP_LOGI(TAG, "初始化 ST7789 LCD...");
    ESP_ERROR_CHECK(st7789_lcd_init());
    st7789_lcd_clear_screen(0x0000);  // 清屏为黑色
    ESP_LOGI(TAG, "ST7789 LCD 初始化完成");
    
    // 启动WiFi
    ESP_LOGI(TAG, "启动 WiFi...");
    ESP_ERROR_CHECK(wifi_start());
    
    // 等待WiFi连接成功
    ESP_LOGI(TAG, "等待 WiFi 连接...");
    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // WiFi连接成功，LED变绿色
    ws2812_led_set_color(0, 30, 0);  // 绿色
    ESP_LOGI(TAG, "LED: 绿色 - WiFi 连接成功");
    
    // 初始化MQTT，注册图像接收回调
    ESP_ERROR_CHECK(mymqtt_init(mqtt_image_callback));
    ESP_LOGI(TAG, "MQTT 初始化完成，等待接收图像数据...");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "请向主题 'esp32/image' 发送 240x240 RGB565 图像数据");
}
