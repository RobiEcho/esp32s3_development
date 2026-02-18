#ifndef __MQTT_APP_CONFIG_H__
#define __MQTT_APP_CONFIG_H__

/* ================= Broker Config ================= */
#define MQTT_APP_BROKER_URI          "mqtt://192.168.5.46:1883"
#define MQTT_APP_CLIENT_ID           "esp32s3_client"
#define MQTT_APP_USERNAME            "RobiEcho"
#define MQTT_APP_PASSWORD            "123456"

/* ================= Buffer Config ================= */
#define MQTT_APP_RX_BUFFER_SIZE      (24 * 1024)

/* ================= Topic Config ================= */
#define MQTT_APP_TOPIC_MPU6050       "esp32s3/mpu6050_data"   // MPU6050 数据主题
#define MQTT_APP_TOPIC_IMAGE         "esp32s3/image"          // 接收图像主题

#endif /* __MQTT_APP_CONFIG_H__ */
