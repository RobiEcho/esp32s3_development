#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "esp_err.h"

/**
 * @brief 初始化命令处理器
 */
esp_err_t command_handler_init(void);

/**
 * @brief 执行语音命令
 * @param command 命令字符串（如 "kai deng", "guan deng"）
 */
void command_handler_execute(const char *command);

#endif /* COMMAND_HANDLER_H */
