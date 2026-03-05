#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "examples.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 开发板启动");
    
    examples_run_demo();
}
