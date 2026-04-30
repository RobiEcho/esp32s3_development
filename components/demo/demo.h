#ifndef DEMO_H
#define DEMO_H

// 示例选择宏定义
#define DEMO_AUDIO_LOOPBACK      0
#define DEMO_LVGL_DISPLAY        1
#define DEMO_LED_EFFECTS         2
#define DEMO_SPEECH_RECOGNITION  3
#define DEMO_MQTT_MPU6050        4
#define DEMO_MQTT_VIDEO          5
#define DEMO_UDP_VIDEO           6

// 选择要运行的示例
#define SELECTED_DEMO 6

void demo_run(void);

#if SELECTED_DEMO == DEMO_AUDIO_LOOPBACK
void demo_audio_loopback(void);
#elif SELECTED_DEMO == DEMO_LVGL_DISPLAY
void demo_lvgl_display(void);
#elif SELECTED_DEMO == DEMO_LED_EFFECTS
void demo_led_effects(void);
#elif SELECTED_DEMO == DEMO_SPEECH_RECOGNITION
void demo_speech_recognition(void);
#elif SELECTED_DEMO == DEMO_MQTT_MPU6050
void demo_wifi_mqtt(void);
#elif SELECTED_DEMO == DEMO_MQTT_VIDEO
void demo_mqtt_video(void);
#elif SELECTED_DEMO == DEMO_UDP_VIDEO
void demo_udp_video(void);
#endif

#endif
