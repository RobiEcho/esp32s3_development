#ifndef __VIDEO_DECODE_H__
#define __VIDEO_DECODE_H__

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 视频帧信息结构体
 */
typedef struct {
    uint16_t width;      /*!< 图像宽度 */
    uint16_t height;     /*!< 图像高度 */
    size_t output_len;   /*!< 解码后 RGB565 数据的实际字节长度 */
} video_frame_info_t;

/**
 * @brief 初始化视频解码模块
 * @note 内部会分配 SPIRAM 用于环形缓冲区和解码临时空间，并初始化硬件 JPEG 解码器
 * @return 
 * - ESP_OK: 初始化成功
 * - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t video_decode_init(void);

/**
 * @brief 去初始化视频解码模块
 * @note 释放所有已分配的信号量、内存缓冲区和解码器句柄
 */
esp_err_t video_decode_deinit(void);

/**
 * @brief 推送 JPEG 数据片断到解码缓冲区
 * * @param data      数据片断指针
 * @param len       当前片断长度
 * @param offset    当前片断在完整帧中的偏移量 (第一包必须为 0)
 * @param total_len 完整一帧的总长度
 * @return 
 * - ESP_OK: 写入成功
 * - ESP_ERR_NO_MEM: 缓冲区或帧邮箱已满
 * - ESP_ERR_INVALID_ARG: 偏移量不连续导致丢包
 */
esp_err_t video_decode_push_data(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len);

/**
 * @brief 执行解码处理流程
 * @note 此函数通常在显示任务循环中调用。它会从队列中获取最新完整帧并进行硬件解码。
 * * @param out_rgb565   指向接收 RGB565 数据的缓冲区指针 (建议大小 >= 240*320*2)
 * @param out_len      提供的输出缓冲区长度
 * @param frame_info   [out] 用于接收解码后的图像元数据（宽、高、实际长度）
 * @return 
 * - ESP_OK: 解码成功
 * - ESP_ERR_NOT_FOUND: 邮箱中暂无待处理的完整帧
 * - ESP_FAIL: 硬件解码失败
 */
esp_err_t video_decode_process(uint16_t *out_rgb565, size_t out_len, video_frame_info_t *frame_info);

#endif /* __VIDEO_DECODE_H__ */