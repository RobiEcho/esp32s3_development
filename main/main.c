#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ws2812_led.h"
#include "st7789_lcd.h"
#include "audio_pipeline.h"
#include "lvgl_port.h"
#include "lvgl_demo_ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ws2812_led_init();
    ws2812_led_start_rainbow();
    
    // 初始化LVGL
    lv_disp_t *disp = lvgl_port_init();
    
    // 启动LVGL任务（在核心0运行）
    lvgl_port_start_task(0);
    
    // 创建UI（需要在互斥锁保护下）
    if (lvgl_port_lock_mutex(1000)) {
        lvgl_demo_ui(disp);
        lvgl_port_unlock_mutex();
    }
    
    // 启动音频处理（在核心1运行）
    audio_pipeline_start(1);

    ESP_LOGI(TAG, "Hello, ESP32-S3!");
}