#ifndef AUDIO_PARTITION_MAP_H
#define AUDIO_PARTITION_MAP_H

#include <stdint.h>
#include <stddef.h>

#define AUDIO_PARTITION_ADDR   0x700000
#define AUDIO_PARTITION_SIZE   0x200000
#define AUDIO_INDEX_SIZE       0x1000
#define AUDIO_INDEX_MAGIC      0x41554449  // "AUDI"
#define MAX_AUDIO_SIZE         (256 * 1024)  // 单个音频最大256KB (约8秒@16kHz)

typedef enum {
    AUDIO_TYPE_WAKEUP_CONFIRM = 0,    // 唤醒确认音 (播放"我在")
    AUDIO_TYPE_COMMAND_CONFIRM = 1,   // 命令确认音 (播放"收到指令")
    AUDIO_TYPE_ERROR = 2,             // 错误提示音 (播放"抱歉,我没听懂")
    AUDIO_TYPE_COMMAND_1 = 3,         // 命令1反馈音 (播放"已关灯")
    AUDIO_TYPE_COMMAND_2 = 4,         // 命令2反馈音 (播放"已开灯")
    AUDIO_TYPE_MAX = 5,
} audio_type_t;

// 索引表头结构 (16字节)
typedef struct __attribute__((packed)) {
    uint32_t magic;      // 魔数: 0x41554449 "AUDI"
    uint32_t count;      // 音频条目数量
    uint32_t reserved1;  // 保留字段
    uint32_t reserved2;  // 保留字段
} audio_index_header_t;

// 索引表条目结构 (16字节)
typedef struct __attribute__((packed)) {
    uint32_t offset;      // 音频数据偏移(相对分区起始)
    uint32_t size;        // 音频数据大小(字节)
    uint32_t sample_rate; // 采样率(Hz)
    uint32_t reserved;    // 保留字段
} audio_index_entry_t;

#endif
