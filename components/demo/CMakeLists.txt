set(src_dirs
    .
    demo_audio_loopback
    demo_led_effects
    demo_lvgl_display
    demo_mqtt_mpu6050
    demo_net_video
    demo_speech_recognition
)

set(include_dirs
    .
)

set(requires
    BSP
    NET
    my_lvgl
    AUDIO
    decode
    cJSON
)

idf_component_register(
    SRC_DIRS      ${src_dirs}
    INCLUDE_DIRS  ${include_dirs}
    REQUIRES      ${requires}
)
