#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "esp_err.h"

esp_err_t nvs_storage_init(void);
esp_err_t nvs_storage_write_str(const char *namespace_name, const char *key, const char *value);
esp_err_t nvs_storage_read_str(const char *namespace_name, const char *key, char *value, size_t value_len);
esp_err_t nvs_storage_write_i32(const char *namespace_name, const char *key, int32_t value);
esp_err_t nvs_storage_read_i32(const char *namespace_name, const char *key, int32_t *value);
esp_err_t nvs_storage_delete_key(const char *namespace_name, const char *key);
esp_err_t nvs_storage_delete_namespace(const char *namespace_name);

#endif /* NVS_STORAGE_H */
