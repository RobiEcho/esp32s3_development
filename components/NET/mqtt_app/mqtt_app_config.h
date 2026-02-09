#ifndef __MQTT_APP_CONFIG_H__
#define __MQTT_APP_CONFIG_H__

/* ================= Broker Config ================= */
#define MQTT_APP_BROKER_URI          "mqtt://192.168.5.46:1883"
#define MQTT_APP_CLIENT_ID           "esp32s3_client"
#define MQTT_APP_USERNAME            "RobiEcho"
#define MQTT_APP_PASSWORD            "123456"

/* ================= Buffer Config ================= */
#define MQTT_APP_RX_BUFFER_SIZE      (16 * 1024)    // 接收缓冲区大小（16KB）

/* ================= Topic Config ================= */
#define MQTT_APP_TOPIC_MPU6050       "esp32s3/mpu6050_data"   // MPU6050 数据主题
#define MQTT_APP_TOPIC_IMAGE         "esp32s3/image"     // 接收图像主题

/* ================= Image Config ================= */
#define MQTT_APP_IMG_WIDTH           240
#define MQTT_APP_IMG_HEIGHT          240
#define MQTT_APP_IMG_PIXEL_SIZE      2              // RGB565: 2字节/像素
#define MQTT_APP_IMG_BUF_SIZE        (MQTT_APP_IMG_WIDTH * MQTT_APP_IMG_HEIGHT * MQTT_APP_IMG_PIXEL_SIZE)
#define MQTT_APP_IMG_TIMEOUT_US      (2000 * 1000)  // 图像接收超时（2秒）

#endif /* __MQTT_APP_CONFIG_H__ */
