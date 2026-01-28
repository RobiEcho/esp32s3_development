#include "audio_pipeline.h"
#include "inmp441_mic.h"
#include "max98357a_amp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_PIPELINE";

#define AUDIO_BLOCK_SIZE    160
#define TASK_STACK_SIZE     (4 * 1024)
#define TASK_PRIORITY       10

typedef struct {
    int32_t *input;
    int16_t *output;
} audio_buffers_t;

static audio_buffers_t s_buffers = {0};
static TaskHandle_t s_task_handle = NULL;
static bool s_running = false;

static void _audio_pipeline_task(void *arg)
{    
    while (s_running) {
        size_t bytes_read = 0;
        esp_err_t ret = inmp441_mic_read(s_buffers.input, AUDIO_BLOCK_SIZE * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        
        if (ret != ESP_OK || bytes_read != AUDIO_BLOCK_SIZE * sizeof(int32_t)) {
            continue;
        }
        
        size_t samples = AUDIO_BLOCK_SIZE;
        
        // int32(高24位有效) -> int16
        for (size_t i = 0; i < samples; i++) {
            int32_t sample = s_buffers.input[i] >> 16;
            s_buffers.output[i] = (int16_t)sample;
        }

        size_t bytes_written = 0;
        max98357a_amp_write(s_buffers.output, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
    
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_pipeline_start(BaseType_t core_id)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }
    
    // 初始化音频硬件
    ESP_LOGI(TAG, "初始化音频硬件...");
    uint32_t dma_desc_num = 6;
    uint32_t dma_frame_num = AUDIO_BLOCK_SIZE;
    
    ESP_ERROR_CHECK(inmp441_mic_init(dma_desc_num, dma_frame_num));
    ESP_ERROR_CHECK(max98357a_amp_init(dma_desc_num, dma_frame_num));
    
#if CONFIG_SPIRAM
    s_buffers.input = heap_caps_malloc(AUDIO_BLOCK_SIZE * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    s_buffers.output = heap_caps_malloc(AUDIO_BLOCK_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
#else
    s_buffers.input = heap_caps_malloc(AUDIO_BLOCK_SIZE * sizeof(int32_t), MALLOC_CAP_DMA);
    s_buffers.output = heap_caps_malloc(AUDIO_BLOCK_SIZE * sizeof(int16_t), MALLOC_CAP_DMA);
#endif
    
    if (!s_buffers.input || !s_buffers.output) {
        if (s_buffers.input) free(s_buffers.input);
        if (s_buffers.output) free(s_buffers.output);
        s_buffers.input = NULL;
        s_buffers.output = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = inmp441_mic_enable();
    if (ret != ESP_OK) {
        audio_pipeline_stop();
        return ret;
    }
    
    ret = max98357a_amp_enable();
    if (ret != ESP_OK) {
        audio_pipeline_stop();
        return ret;
    }
    
    s_running = true;
    
    BaseType_t task_ret;
    if (core_id == tskNO_AFFINITY) {
        task_ret = xTaskCreate(
            _audio_pipeline_task,
            "audio_pipeline",
            TASK_STACK_SIZE,
            NULL,
            TASK_PRIORITY,
            &s_task_handle
        );
    } else {
        task_ret = xTaskCreatePinnedToCore(
            _audio_pipeline_task,
            "audio_pipeline",
            TASK_STACK_SIZE,
            NULL,
            TASK_PRIORITY,
            &s_task_handle,
            core_id
        );
    }
    
    if (task_ret != pdPASS) {
        audio_pipeline_stop();
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "音频处理已启动 (CPU: %s)", 
             core_id == tskNO_AFFINITY ? "any" : (core_id == 0 ? "0" : "1"));
    return ESP_OK;
}

esp_err_t audio_pipeline_stop(void)
{
    s_running = false;
    
    if (s_task_handle != NULL) {
        while (s_task_handle != NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    inmp441_mic_disable();
    max98357a_amp_disable();
    
    if (s_buffers.input) {
        free(s_buffers.input);
        s_buffers.input = NULL;
    }
    if (s_buffers.output) {
        free(s_buffers.output);
        s_buffers.output = NULL;
    }
    
    return ESP_OK;
}
