#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include "audio_partition_map.h"
#include "esp_err.h"

/**
 * @brief 初始化音频播放器
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t audio_player_init(void);

/**
 * @brief 播放指定类型的音频
 * @param type 音频类型
 * @return ESP_OK 成功, 其他值表示失败
 * @note 此函数会阻塞直到播放完成
 */
esp_err_t audio_player_play(audio_type_t type);

/**
 * @brief 获取音频信息
 * @param type 音频类型
 * @param size 输出参数,音频大小(字节)
 * @param sample_rate 输出参数,采样率(Hz),可为NULL
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t audio_player_get_info(audio_type_t type, uint32_t *size, uint32_t *sample_rate);

/**
 * @brief 停止当前播放
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t audio_player_stop(void);

#endif
