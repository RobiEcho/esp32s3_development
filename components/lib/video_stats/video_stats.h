#ifndef __VIDEO_STATS_H__
#define __VIDEO_STATS_H__

#include <stdint.h>
#include "freertos/FreeRTOS.h"

typedef struct video_stats_t {
    volatile uint32_t frame_decoded;       // 成功解码的帧数
    volatile uint32_t frame_displayed;     // 成功显示的帧数
    volatile uint32_t frame_dropped;       // 丢弃的帧数（缓冲区满）
    portMUX_TYPE lock;
} video_stats_t;

/**
 * @brief 创建视频统计结构
 */
video_stats_t* video_stats_create(void);

/**
 * @brief 销毁视频统计结构
 */
void video_stats_destroy(video_stats_t* stats);

/**
 * @brief 重置统计数据
 */
void video_stats_reset(video_stats_t* stats);

/**
 * @brief 记录解码成功
 */
void video_stats_inc_decoded(video_stats_t* stats);

/**
 * @brief 记录显示成功
 */
void video_stats_inc_displayed(video_stats_t* stats);

/**
 * @brief 记录丢帧
 */
void video_stats_inc_dropped(video_stats_t* stats);

/**
 * @brief 获取统计快照（线程安全）
 * @param decoded 输出解码帧数
 * @param displayed 输出显示帧数
 * @param dropped 输出丢帧数
 */
void video_stats_get(video_stats_t* stats, uint32_t* decoded, uint32_t* displayed, uint32_t* dropped);

#endif // __VIDEO_STATS_H__
