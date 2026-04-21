#include "demo.h"

#if SELECTED_DEMO == DEMO_AUDIO_LOOPBACK
#include "audio_pipeline.h"
#include "esp_log.h"

static const char *TAG = "demo_audio";

void demo_audio_loopback(void)
{
    ESP_LOGI(TAG, "麦克风采集的声音会直接从扬声器播放");
    
    // 启动音频管道（CPU0）
    ESP_ERROR_CHECK(audio_pipeline_start(0));
    
    ESP_LOGI(TAG, "音频直通已启动，对着麦克风说话试试");
}
#endif
