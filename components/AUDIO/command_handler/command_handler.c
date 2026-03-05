#include "command_handler.h"
#include "ws2812_led.h"
#include "speech_recognition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "command_handler";

// 去除前导空格
static const char *_skip_leading_space(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

esp_err_t command_handler_init(void)
{
    ESP_LOGI(TAG, "命令处理器初始化完成");
    return ESP_OK;
}

void command_handler_execute(const char *command)
{
    if (command == NULL) {
        return;
    }
    
    const char *cmd = _skip_leading_space(command);
    
    ESP_LOGI(TAG, "执行命令: '%s'", cmd);
    
    if (strcmp(cmd, "kai deng") == 0) {
        ESP_LOGI(TAG, "开灯");
        ws2812_led_start_rainbow();
        // 触发命令2反馈音频
        speech_recognition_trigger_audio(AUDIO_PLAY_COMMAND_2);
    } else if (strcmp(cmd, "guan deng") == 0) {
        ESP_LOGI(TAG, "关灯");
        ws2812_led_clear();
        // 触发命令1反馈音频
        speech_recognition_trigger_audio(AUDIO_PLAY_COMMAND_1);
    } else {
        ESP_LOGW(TAG, "未识别的命令: '%s'", cmd);
        // 触发错误提示音频
        speech_recognition_trigger_audio(AUDIO_PLAY_ERROR);
    }
}
