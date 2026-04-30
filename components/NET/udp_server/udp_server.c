#include "udp_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "udp_server";

#define UDP_RECV_BUFFER_SIZE 2048

static int s_sock = -1;
static TaskHandle_t s_task_handle = NULL;
static udp_recv_callback_t s_recv_callback = NULL;
static volatile bool s_running = false;

static void udp_server_task(void *pvParameters)
{
    uint8_t *rx_buffer = heap_caps_malloc(UDP_RECV_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!rx_buffer) {
        ESP_LOGE(TAG, "接收缓冲区分配失败");
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "UDP 服务器任务已启动");

    while (s_running) {
        // 接收 UDP 数据
        int len = recvfrom(s_sock, rx_buffer, UDP_RECV_BUFFER_SIZE, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            // errno 11 (EAGAIN/EWOULDBLOCK) 是超时，继续循环
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            // 其他错误才退出
            if (s_running) {
                ESP_LOGE(TAG, "recvfrom 失败: errno %d", errno);
            }
            break;
        } else if (len > 0) {
            // 获取发送方地址
            char addr_str[32];
            inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
            uint16_t port = ntohs(source_addr.sin_port);

            // 调用回调函数
            if (s_recv_callback) {
                s_recv_callback(rx_buffer, len, addr_str, port);
            }
        }
    }

    free(rx_buffer);
    ESP_LOGI(TAG, "UDP 服务器任务已退出");
    vTaskDelete(NULL);
}

esp_err_t udp_server_start(uint16_t port, udp_recv_callback_t recv_callback)
{
    if (s_running) {
        ESP_LOGW(TAG, "UDP 服务器已在运行");
        return ESP_ERR_INVALID_STATE;
    }

    if (!recv_callback) {
        ESP_LOGE(TAG, "回调函数不能为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 创建 socket
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "创建 socket 失败: errno %d", errno);
        return ESP_FAIL;
    }

    // 设置接收超时（用于检查 s_running 标志）
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // 启用广播选项（允许发送广播包）
    int broadcast_enable = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    // 绑定端口
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    int err = bind(s_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "绑定端口 %d 失败: errno %d", port, errno);
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UDP 服务器监听端口 %d", port);

    s_recv_callback = recv_callback;
    s_running = true;

    // 创建接收任务（固定在 CPU0）
    BaseType_t ret = xTaskCreatePinnedToCore(udp_server_task, "udp_server", 4096, NULL, 8, &s_task_handle, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建任务失败");
        close(s_sock);
        s_sock = -1;
        s_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t udp_server_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "停止 UDP 服务器");
    s_running = false;

    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);
        close(s_sock);
        s_sock = -1;
    }

    // 等待任务退出
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_task_handle = NULL;
    }

    return ESP_OK;
}

esp_err_t udp_server_send(const char *dest_addr, uint16_t dest_port,
                          const uint8_t *data, size_t len)
{
    if (s_sock < 0) {
        ESP_LOGE(TAG, "UDP 服务器未启动");
        return ESP_ERR_INVALID_STATE;
    }

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_addr, &dest.sin_addr);

    int err = sendto(s_sock, data, len, 0, (struct sockaddr *)&dest, sizeof(dest));
    if (err < 0) {
        ESP_LOGE(TAG, "发送失败: errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "发送 %d 字节到 %s:%d", len, dest_addr, dest_port);
    return ESP_OK;
}

esp_err_t udp_server_broadcast(uint16_t port, const uint8_t *data, size_t len)
{
    if (s_sock < 0) {
        ESP_LOGE(TAG, "UDP 服务器未启动");
        return ESP_ERR_INVALID_STATE;
    }

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);  // 255.255.255.255

    int err = sendto(s_sock, data, len, 0, (struct sockaddr *)&dest, sizeof(dest));
    if (err < 0) {
        ESP_LOGE(TAG, "广播失败: errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "广播 %d 字节到端口 %d", len, port);
    return ESP_OK;
}
