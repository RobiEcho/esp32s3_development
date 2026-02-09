#ifndef SPEECH_RECOGNITION_H
#define SPEECH_RECOGNITION_H

#include "esp_err.h"

/**
 * @brief 语音命令回调函数类型
 * @param command 识别到的命令字符串
 */
typedef void (*speech_command_callback_t)(const char *command);

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

#endif /* SPEECH_RECOGNITION_H */
