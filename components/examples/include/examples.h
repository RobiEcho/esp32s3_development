#ifndef EXAMPLES_H
#define EXAMPLES_H

// 示例选择宏定义
#define EXAMPLE_AUDIO_LOOPBACK      0
#define EXAMPLE_LVGL_DISPLAY        1
#define EXAMPLE_LED_EFFECTS         2
#define EXAMPLE_SPEECH_RECOGNITION  3
#define EXAMPLE_MQTT_MPU6050        4
#define EXAMPLE_MQTT_IMAGE          5

// 选择要运行的示例
#define SELECTED_EXAMPLE  EXAMPLE_SPEECH_RECOGNITION

void examples_run_demo(void);
void example_audio_loopback(void);
void example_lvgl_display(void);
void example_led_effects(void);
void example_speech_recognition(void);
void example_wifi_mqtt(void);
void example_mqtt_image(void);

#endif
