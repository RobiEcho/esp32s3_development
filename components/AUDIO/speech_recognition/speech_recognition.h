#ifndef SPEECH_RECOGNITION_H
#define SPEECH_RECOGNITION_H

#include "esp_err.h"

/**
 * @brief 语音命令回调函数类型
 * @param command 识别到的命令字符串
 */
typedef void (*speech_command_callback_t)(const char *command);

/**
 * @brief 音频播放类型
 */
typedef enum {
    AUDIO_PLAY_WAKEUP_CONFIRM = 0,  // 唤醒确认
    AUDIO_PLAY_COMMAND_CONFIRM,     // 命令确认
    AUDIO_PLAY_ERROR,               // 错误提示
    AUDIO_PLAY_COMMAND_1,           // 命令1反馈
    AUDIO_PLAY_COMMAND_2,           // 命令2反馈
} audio_play_type_t;

/**
 * @brief 初始化语音识别模块
 * @param callback 命令识别回调函数
 */
esp_err_t speech_recognition_init(speech_command_callback_t callback);

/**
 * @brief 启动语音识别
 */
esp_err_t speech_recognition_start(void);

/**
 * @brief 停止语音识别
 */
esp_err_t speech_recognition_stop(void);

/**
 * @brief 触发音频播放
 * @param type 音频类型
 * @return ESP_OK 成功, ESP_FAIL 失败
 */
esp_err_t speech_recognition_trigger_audio(audio_play_type_t type);

#endif /* SPEECH_RECOGNITION_H */
