#include "examples.h"
#include "lvgl_port.h"
#include "lvgl_demo_ui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "example_lvgl";

void example_lvgl_display(void)
{
    ESP_LOGI(TAG, "=== LVGL 显示测试 ===");
    
    // 初始化 LVGL
    lv_disp_t *disp = lvgl_port_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL 初始化失败");
        return;
    }
    
    // 启动 LVGL 任务（CPU1）
    ESP_ERROR_CHECK(lvgl_port_start_task(1));
    
    // 等待 LVGL 任务完全启动
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 创建 Demo UI（需要加锁）
    if (lvgl_port_lock_mutex(1000)) {
        lvgl_demo_ui(disp);
        lvgl_port_unlock_mutex();
    } else {
        ESP_LOGE(TAG, "无法获取 LVGL 互斥锁");
        return;
    }
    
    ESP_LOGI(TAG, "LVGL 显示测试已启动");
}
