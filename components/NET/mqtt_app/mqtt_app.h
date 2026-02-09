#ifndef __MQTT_APP_H__
#define __MQTT_APP_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef esp_err_t (*mqtt_data_handler_t)(const uint8_t *data, size_t len);

esp_err_t mqtt_app_init(void);
esp_err_t mqtt_app_register_data_handler(mqtt_data_handler_t handler);
bool mqtt_app_is_inited(void);
bool mqtt_app_is_connected(void);
esp_err_t mqtt_app_publish(const char *topic, const void *data, size_t len, int qos);
esp_err_t mqtt_app_subscribe(const char *topic, int qos);
esp_err_t mqtt_app_unsubscribe(const char *topic);

#endif /* __MQTT_APP_H__ */
