#ifndef __RING_BUF_H__
#define __RING_BUF_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct ring_buf_t {
    uint8_t *buf;
    size_t size;
    volatile size_t write_pos;
    volatile size_t read_pos;
    portMUX_TYPE lock;
} ring_buf_t;

/**
 * @brief 创建环形缓冲区
 * @param size 缓冲区大小（单位：字节）
 * @param caps 内存属性（如 MALLOC_CAP_SPIRAM）
 */
ring_buf_t* ring_buf_create(size_t size, uint32_t caps);

/**
 * @brief 销毁环形缓冲区
 */
void ring_buf_destroy(ring_buf_t* rb);

/**
 * @brief 向缓冲区写入数据
 */
esp_err_t ring_buf_write(ring_buf_t* rb, const uint8_t *data, size_t len);

/**
 * @brief 从缓冲区读取数据
 * @param offset 帧的起始位置（从 ring_buf_get_write_pos 获取）
 * @param len 要读取的长度
 */
esp_err_t ring_buf_read(ring_buf_t* rb, uint8_t *out_data, size_t offset, size_t len);

/**
 * @brief 获取当前写入位置（用于记录帧起始点）
 */
size_t ring_buf_get_write_pos(ring_buf_t* rb);

/**
 * @brief 释放已处理的帧空间
 * @param next_frame_start 下一帧的起始位置
 */
void ring_buf_finish_frame(ring_buf_t* rb, size_t next_frame_start);

/**
 * @brief 获取缓冲区剩余空间
 */
size_t ring_buf_get_free_size(ring_buf_t* rb);

/**
 * @brief 获取缓冲区已用空间
 */
size_t ring_buf_get_used_size(ring_buf_t* rb);

#endif /* __RING_BUF_H__ */