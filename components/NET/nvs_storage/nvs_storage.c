#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"

static bool s_inited = false;

esp_err_t nvs_storage_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    s_inited = true;
    return ESP_OK;
}

esp_err_t nvs_storage_write_str(const char *namespace_name, const char *key, const char *value)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (namespace_name == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(namespace_name, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, key, value));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    
    return ESP_OK;
}

esp_err_t nvs_storage_read_str(const char *namespace_name, const char *key, char *value, size_t value_len)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (namespace_name == NULL || key == NULL || value == NULL || value_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = value_len;
    err = nvs_get_str(nvs_handle, key, value, &required_size);
    nvs_close(nvs_handle);
    
    return err;
}

esp_err_t nvs_storage_write_i32(const char *namespace_name, const char *key, int32_t value)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (namespace_name == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(namespace_name, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_i32(nvs_handle, key, value));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    
    return ESP_OK;
}

esp_err_t nvs_storage_read_i32(const char *namespace_name, const char *key, int32_t *value)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (namespace_name == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_i32(nvs_handle, key, value);
    nvs_close(nvs_handle);
    
    return err;
}

esp_err_t nvs_storage_delete_key(const char *namespace_name, const char *key)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (namespace_name == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(namespace_name, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_erase_key(nvs_handle, key));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    
    return ESP_OK;
}

esp_err_t nvs_storage_delete_namespace(const char *namespace_name)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (namespace_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(namespace_name, NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_erase_all(nvs_handle));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
    
    return ESP_OK;
}
