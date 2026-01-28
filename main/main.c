#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "ws2812_led.h"
#include "audio_pipeline.h"
#include "lvgl_port.h"
#include "lvgl_demo_ui.h"
#include "wifi_manager.h"
#include "mymqtt.h"
#include "mpu6050.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "main";

// MPU6050数据队列
static QueueHandle_t g_mpu6050_queue = NULL;

// MPU6050生产者任务 - 读取原始数据并放入队列
static void mpu6050_producer_task(void *arg)
{
    (void)arg;
    mpu6050_raw_data_t raw_data;
    
    while (1) {
        // 读取MPU6050原始数据
        if (mpu6050_read_raw_data(&raw_data) == ESP_OK) {
            // 发送到队列（如果队列满了，等待100ms）
            if (xQueueSend(g_mpu6050_queue, &raw_data, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "MPU6050队列已满，丢弃数据");
            }
        } else {
            ESP_LOGE(TAG, "MPU6050读取失败");
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));  // 0.5秒读取一次
    }
}

// MQTT消费者任务 - 从队列取数据并发送
static void mqtt_consumer_task(void *arg)
{
    (void)arg;
    mpu6050_raw_data_t raw_data;
    mpu6050_data_t converted_data;
    char mqtt_payload[256];
    
    while (1) {
        // 从队列接收数据
        if (xQueueReceive(g_mpu6050_queue, &raw_data, portMAX_DELAY) == pdTRUE) {
            // 转换为物理单位
            ESP_ERROR_CHECK(mpu6050_convert_data(&raw_data, &converted_data));

            if (mymqtt_is_connected()) {
                // 构造JSON格式的MQTT消息
                snprintf(mqtt_payload, sizeof(mqtt_payload),
                         "{\"gyro\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
                         "\"accel\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}}",
                         converted_data.gyro_x, converted_data.gyro_y, converted_data.gyro_z,
                         converted_data.accel_x, converted_data.accel_y, converted_data.accel_z);
                
                // 发布到MQTT主题
                int ret = mymqtt_publish("esp32/mpu6050_data", mqtt_payload, strlen(mqtt_payload), 0);
                if (ret < 0) {
                    ESP_LOGW(TAG, "MQTT发送失败");
                }
            }
        }
    }
}

void app_main(void)
{
    ws2812_led_init();
    ws2812_led_set_color(30, 0, 0);  // 红色
    
    // 启动WiFi
    ESP_LOGI(TAG, "启动 WiFi...");
    ESP_ERROR_CHECK(wifi_start());
    
    // 初始化MPU6050
    ESP_LOGI(TAG, "初始化 MPU6050...");
    ESP_ERROR_CHECK(mpu6050_init());
    ESP_LOGI(TAG, "MPU6050 初始化完成");
    
    // 创建MPU6050数据队列（队列长度为10）
    g_mpu6050_queue = xQueueCreate(10, sizeof(mpu6050_raw_data_t));
    if (g_mpu6050_queue == NULL) {
        ESP_LOGE(TAG, "创建MPU6050队列失败");
        return;
    }
    
    // 初始化LVGL
    lv_disp_t *disp = lvgl_port_init();
    
    // 启动LVGL任务（在核心0运行）
    lvgl_port_start_task(0);
    
    // 创建UI（需要在互斥锁保护下）
    if (lvgl_port_lock_mutex(1000)) {
        lvgl_demo_ui(disp);
        lvgl_port_unlock_mutex();
    }
    ESP_LOGI(TAG, "LVGL UI 初始化完成");
    
    // 启动音频直通（在核心1运行）
    audio_pipeline_start(1);
    ESP_LOGI(TAG, "音频直通启动");

    // 等待WiFi连接成功
    while (wifi_get_state() != WIFI_STATE_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // WiFi连接成功，LED变绿色并初始化MQTT
    ws2812_led_set_color(0, 30, 0);  // 绿色
    ESP_LOGI(TAG, "WiFi 连接成功");
    ESP_ERROR_CHECK(mymqtt_init(NULL));
    ESP_LOGI(TAG, "MQTT 初始化完成");
    
    // 创建MPU6050生产者任务（在核心1运行）
    xTaskCreatePinnedToCore(mpu6050_producer_task, "mpu6050_prod", 3072, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "MPU6050生产者任务已启动（CPU1）");
    
    // 创建MQTT消费者任务
    xTaskCreate(mqtt_consumer_task, "mqtt_consumer", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "MQTT消费者任务已启动");

    ESP_LOGI(TAG, "系统初始化完成！");
}