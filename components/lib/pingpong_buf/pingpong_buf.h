#ifndef __PINGPONG_BUF_H__
#define __PINGPONG_BUF_H__

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

typedef enum {
    PPB_IDLE = 0,      // 缓冲区空闲，可用于解码/填充
    PPB_DECODING,      // 正在填充数据中
    PPB_READY,         // 数据填充完成，等待显示/使用
    PPB_DISPLAYING     // 正在被显示设备（如DMA）占用
} ppb_status_t;

typedef enum {
    PPB_CONTEXT_TASK = 0,  // 普通任务上下文
    PPB_CONTEXT_ISR        // 中断服务程序上下文
} ppb_context_t;

typedef struct {
    uint16_t *data;
    volatile ppb_status_t status;
} ppb_block_t;

typedef struct pingpong_buf_t {
    ppb_block_t blocks[2];
    size_t buf_size;       // 记录缓冲区大小
    portMUX_TYPE lock;     // 自旋锁
} pingpong_buf_t;

/**
 * @brief 创建双缓冲句柄
 * @param buf_size 每个缓冲区的大小（单位：字节）
 * @param caps 内存属性（如 MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA）
 * @return pingpong_buf_t* 双缓冲句柄，失败返回 NULL
 */
pingpong_buf_t* ppb_create(size_t buf_size, uint32_t caps);

/**
 * @brief 销毁双缓冲句柄，释放所有资源
 * @param ppb 双缓冲句柄
 */
void ppb_destroy(pingpong_buf_t* ppb);

/**
 * @brief 获取一个空闲的缓冲区用于解码
 * @param[out] index 返回缓冲区的索引 (0 或 1)
 * @return void* 缓冲区指针，若无空闲则返回 NULL
 */
void* ppb_get_idle_block(pingpong_buf_t* ppb, uint8_t *index);

/**
 * @brief 标记指定缓冲区为 READY 状态（解码完成）
 */
void ppb_set_ready(pingpong_buf_t* ppb, uint8_t index);

/**
 * @brief 获取一个处于 READY 状态的缓冲区用于显示
 * @param[out] index 返回缓冲区的索引
 * @return void* 缓冲区指针，若无数据则返回 NULL
 */
void* ppb_get_ready_block(pingpong_buf_t* ppb, uint8_t *index);

/**
 * @brief 标记缓冲区为 IDLE（显示完成）
 * @param ppb 双缓冲句柄
 * @param index 缓冲区索引
 * @param context 调用上下文（PPB_CONTEXT_TASK 或 PPB_CONTEXT_ISR）
 */
void ppb_release(pingpong_buf_t* ppb, uint8_t index, ppb_context_t context);

/**
 * @brief 标记正在显示
 */
void ppb_set_displaying(pingpong_buf_t* ppb, uint8_t index);

#endif // __PINGPONG_BUF_H__