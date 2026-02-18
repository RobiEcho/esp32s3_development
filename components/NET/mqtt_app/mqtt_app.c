#include "mqtt_app.h"
#include "mqtt_app_config.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "mqtt_app";

static esp_mqtt_client_handle_t s_hmqtt = NULL;
static bool s_inited = false;
static volatile bool s_connected = false;

static mqtt_data_handler_t s_data_handler = NULL;

// MQTT 事件处理
static void _mqtt_app_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "已连接");
        s_connected = true;
        // 自动订阅图像主题
        if (s_data_handler) {
            esp_mqtt_client_subscribe(s_hmqtt, MQTT_APP_TOPIC_IMAGE, 1);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "已断开");
        s_connected = false;
        break;

    case MQTT_EVENT_DATA:
        if (s_data_handler && event->data && event->data_len > 0) {
            s_data_handler((const uint8_t *)event->data, event->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "错误: %d", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

esp_err_t mqtt_app_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    // MQTT 客户端配置
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_APP_BROKER_URI,                   // 代理服务器地址
        .credentials.client_id = MQTT_APP_CLIENT_ID,                 // 客户端ID
        .credentials.username = MQTT_APP_USERNAME,                   // 用户名
        .credentials.authentication.password = MQTT_APP_PASSWORD,    // 密码
        .buffer.size = MQTT_APP_RX_BUFFER_SIZE,                      // 接收缓冲区大小
        .network.disable_auto_reconnect = false,                     // 启用自动重连
    };

    // 创建 MQTT 客户端
    s_hmqtt = esp_mqtt_client_init(&cfg);
    if (s_hmqtt == NULL) {
        ESP_LOGE(TAG, "客户端初始化失败");
        return ESP_FAIL;
    }

    // 注册事件回调
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_hmqtt, ESP_EVENT_ANY_ID, _mqtt_app_event_handler, NULL));

    // 启动客户端
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_hmqtt));

    s_inited = true;
    ESP_LOGI(TAG, "初始化完成");
    return ESP_OK;
}

bool mqtt_app_is_inited(void)
{
    return s_inited;
}

esp_err_t mqtt_app_register_data_handler(mqtt_data_handler_t handler)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_data_handler = handler;
    ESP_LOGI(TAG, "数据处理回调已注册");
    return ESP_OK;
}

bool mqtt_app_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_app_publish(const char *topic, const void *data, size_t len, int qos)
{
    if (!s_inited || !s_connected) return ESP_ERR_INVALID_STATE;
    if (topic == NULL || data == NULL) return ESP_ERR_INVALID_ARG;

    int ret = esp_mqtt_client_publish(s_hmqtt, topic, data, len, qos, 0);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_app_subscribe(const char *topic, int qos)
{
    if (!s_inited || topic == NULL) return ESP_ERR_INVALID_ARG;

    int ret = esp_mqtt_client_subscribe(s_hmqtt, topic, qos);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_app_unsubscribe(const char *topic)
{
    if (!s_inited || topic == NULL) return ESP_ERR_INVALID_ARG;

    int ret = esp_mqtt_client_unsubscribe(s_hmqtt, topic);
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}
