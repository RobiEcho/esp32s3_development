#include "examples.h"
#include "wifi_manager.h"
#include "mqtt_app.h"
#include "mqtt_app_config.h"
#include "mpu6050.h"
#include "ws2812_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt_mpu6050";

// MPU6050 数据发布任务
static void mpu6050_mqtt_task(void *arg)
{
    mpu6050_raw_data_t raw_data;
    mpu6050_data_t converted_data;
    char mqtt_payload[256];
    
    ESP_LOGI(TAG, "MPU6050 数据发布任务已启动");
    
    while (1) {
        // 读取 MPU6050 原始数据
        if (mpu6050_read_raw_data(&raw_data) == ESP_OK) {
            // 转换为物理单位
            if (mpu6050_convert_data(&raw_data, &converted_data) == ESP_OK) {
                // 检查 MQTT 是否已连接
                if (mqtt_app_is_connected()) {
                    // 构造 JSON 格式的 MQTT 消息
                    snprintf(mqtt_payload, sizeof(mqtt_payload),
                            "{\"gyro\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
                            "\"accel\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}}",
                            converted_data.gyro_x, converted_data.gyro_y, converted_data.gyro_z,
                            converted_data.accel_x, converted_data.accel_y, converted_data.accel_z);
                    
                    // 发布到 MQTT 主题
                    int ret = mqtt_app_publish(MQTT_APP_TOPIC_MPU6050, mqtt_payload, strlen(mqtt_payload), 0);
                    if (ret < 0) {
                        ESP_LOGW(TAG, "MQTT 发送失败");
                    } else {
                        ESP_LOGI(TAG, "已发布: %s", mqtt_payload);
                    }
                } else {
                    ESP_LOGW(TAG, "MQTT 未连接，跳过发送");
                }
            }
        } else {
            ESP_LOGE(TAG, "MPU6050 读取失败");
        }
        
        // 每 0.5 秒发送一次
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void example_wifi_mqtt(void)
{
    ESP_LOGI(TAG, "=== WiFi + MQTT + MPU6050 测试 ===");
    
    // 0. 初始化 LED（红色：启动中）
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);  // 红色
    ESP_LOGI(TAG, "LED: 红色（启动中）");
    
    // 1. 初始化 MPU6050
    ESP_LOGI(TAG, "初始化 MPU6050...");
    ESP_ERROR_CHECK(mpu6050_init());
    ESP_LOGI(TAG, "MPU6050 初始化完成");
    
    // 2. 启动 WiFi
    ESP_LOGI(TAG, "连接 WiFi...");
    ESP_ERROR_CHECK(wifi_start());
    
    // 3. 等待 WiFi 连接
    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // WiFi 连接成功（黄色）
    ws2812_led_set_color(30, 30, 0);  // 黄色
    ESP_LOGI(TAG, "WiFi 已连接");
    ESP_LOGI(TAG, "LED: 黄色（WiFi 已连接）");
    
    // 4. 初始化 MQTT
    ESP_LOGI(TAG, "初始化 MQTT...");
    ESP_ERROR_CHECK(mqtt_app_init());
    
    // 5. 等待 MQTT 连接
    while (!mqtt_app_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // MQTT 连接成功（绿色）
    ws2812_led_set_color(0, 30, 0);  // 绿色
    ESP_LOGI(TAG, "MQTT 已连接");
    ESP_LOGI(TAG, "LED: 绿色（MQTT 已连接）");
    
    // 6. 创建 MPU6050 数据发布任务
    xTaskCreate(mpu6050_mqtt_task, "mpu_mqtt", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "MPU6050 数据发布任务已创建");
    ESP_LOGI(TAG, "开始每 0.5 秒发布传感器数据到 MQTT...");
}
