#include "examples.h"
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "mqtt_app_config.h"
#include "st7789_lcd.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "example_mqtt_image";

// MQTT 图像数据处理回调 - 将接收到的图像数据直接绘制到 ST7789
static esp_err_t mqtt_app_image_handler(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "接收到空图像数据");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "接收到完整图像帧，开始绘制...");
    
    // 绘制 240x240 的 RGB565 图像到 ST7789
    st7789_lcd_draw_bitmap(0, 0, 240, 240, (void *)data);
    
    ESP_LOGI(TAG, "图像绘制完成");
    return ESP_OK;
}

void example_mqtt_image(void)
{
    ESP_LOGI(TAG, "=== MQTT 图像接收测试 ===");
    
    // 1. 初始化 LED（红色：启动中）
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);  // 红色
    ESP_LOGI(TAG, "LED: 红色（启动中）");
    
    // 2. 初始化 ST7789 LCD
    ESP_LOGI(TAG, "初始化 ST7789 LCD...");
    ESP_ERROR_CHECK(st7789_lcd_init());
    st7789_lcd_clear_screen(0x0000);  // 清屏为黑色
    ESP_LOGI(TAG, "ST7789 LCD 初始化完成");
    
    // 3. 启动 WiFi
    ESP_LOGI(TAG, "连接 WiFi...");
    ESP_ERROR_CHECK(wifi_start());
    
    // 4. 等待 WiFi 连接
    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // WiFi 连接成功（黄色）
    ws2812_led_set_color(30, 30, 0);  // 黄色
    ESP_LOGI(TAG, "WiFi 已连接");
    ESP_LOGI(TAG, "LED: 黄色（WiFi 已连接）");
    
    // 5. 初始化 MQTT
    ESP_LOGI(TAG, "初始化 MQTT...");
    ESP_ERROR_CHECK(mqtt_app_init());
    
    // 注册图像数据处理回调
    ESP_ERROR_CHECK(mqtt_app_register_data_handler(mqtt_app_image_handler));
    
    // 6. 等待 MQTT 连接
    while (!mqtt_app_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // MQTT 连接成功（绿色）
    ws2812_led_set_color(0, 30, 0);  // 绿色
    ESP_LOGI(TAG, "MQTT 已连接");
    ESP_LOGI(TAG, "LED: 绿色（MQTT 已连接）");
    
    ESP_LOGI(TAG, "等待接收图像数据...");
}
