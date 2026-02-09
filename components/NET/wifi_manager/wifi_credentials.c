#include "wifi_credentials.h"
#include "nvs_storage.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "wifi_credentials";

#define WIFI_STA_NAMESPACE  "wifi_sta"
#define WIFI_AP_NAMESPACE   "wifi_ap"
#define SSID_KEY            "ssid"
#define PASSWORD_KEY        "password"

esp_err_t wifi_credentials_save_sta(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(nvs_storage_write_str(WIFI_STA_NAMESPACE, SSID_KEY, ssid));

    if (password != NULL) {
        ESP_ERROR_CHECK(nvs_storage_write_str(WIFI_STA_NAMESPACE, PASSWORD_KEY, password));
    }

    ESP_LOGI(TAG, "STA credentials saved");
    return ESP_OK;
}

esp_err_t wifi_credentials_load_sta(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    if (ssid == NULL || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(nvs_storage_read_str(WIFI_STA_NAMESPACE, SSID_KEY, ssid, ssid_len));

    if (password != NULL && pass_len > 0) {
        esp_err_t err = nvs_storage_read_str(WIFI_STA_NAMESPACE, PASSWORD_KEY, password, pass_len);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to read STA password");
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t wifi_credentials_save_ap(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(nvs_storage_write_str(WIFI_AP_NAMESPACE, SSID_KEY, ssid));

    if (password != NULL) {
        ESP_ERROR_CHECK(nvs_storage_write_str(WIFI_AP_NAMESPACE, PASSWORD_KEY, password));
    }

    ESP_LOGI(TAG, "AP credentials saved");
    return ESP_OK;
}

esp_err_t wifi_credentials_load_ap(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    if (ssid == NULL || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(nvs_storage_read_str(WIFI_AP_NAMESPACE, SSID_KEY, ssid, ssid_len));

    if (password != NULL && pass_len > 0) {
        esp_err_t err = nvs_storage_read_str(WIFI_AP_NAMESPACE, PASSWORD_KEY, password, pass_len);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to read AP password");
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t wifi_credentials_erase_sta(void)
{
    return nvs_storage_delete_namespace(WIFI_STA_NAMESPACE);
}

esp_err_t wifi_credentials_erase_ap(void)
{
    return nvs_storage_delete_namespace(WIFI_AP_NAMESPACE);
}
