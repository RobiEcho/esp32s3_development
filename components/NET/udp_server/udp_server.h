#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// UDP 数据接收回调函数类型
// data: 接收到的数据
// len: 数据长度
// src_addr: 发送方 IP 地址字符串
// src_port: 发送方端口
typedef void (*udp_recv_callback_t)(const uint8_t *data, size_t len, 
                                     const char *src_addr, uint16_t src_port);

/**
 * @brief 初始化并启动 UDP 服务器
 * @param port 监听端口
 * @param recv_callback 数据接收回调函数
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t udp_server_start(uint16_t port, udp_recv_callback_t recv_callback);

/**
 * @brief 停止 UDP 服务器
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t udp_server_stop(void);

/**
 * @brief 发送 UDP 数据到指定地址
 * @param dest_addr 目标 IP 地址字符串
 * @param dest_port 目标端口
 * @param data 要发送的数据
 * @param len 数据长度
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t udp_server_send(const char *dest_addr, uint16_t dest_port, 
                          const uint8_t *data, size_t len);

/**
 * @brief 广播 UDP 数据到局域网
 * @param port 目标端口
 * @param data 要发送的数据
 * @param len 数据长度
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t udp_server_broadcast(uint16_t port, const uint8_t *data, size_t len);

#endif
