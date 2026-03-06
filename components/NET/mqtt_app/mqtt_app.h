#ifndef __MQTT_APP_H__
#define __MQTT_APP_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief MQTT 数据处理函数指针声明
 * @param data      当前分片数据指针 (event->data)
 * @param len       当前分片数据长度 (event->data_len)
 * @param offset    当前分片在完整消息中的偏移 (event->current_data_offset)
 * @param total_len 完整消息的总长度 (event->total_data_len)
 */
typedef esp_err_t (*mqtt_data_handler_t)(const uint8_t *data, size_t len, uint32_t offset, uint32_t total_len);

esp_err_t mqtt_app_init(void);
esp_err_t mqtt_app_register_data_handler(mqtt_data_handler_t handler);
bool mqtt_app_is_inited(void);
bool mqtt_app_is_connected(void);
esp_err_t mqtt_app_publish(const char *topic, const void *data, size_t len, int qos);
esp_err_t mqtt_app_subscribe(const char *topic, int qos);
esp_err_t mqtt_app_unsubscribe(const char *topic);

#endif /* __MQTT_APP_H__ */
