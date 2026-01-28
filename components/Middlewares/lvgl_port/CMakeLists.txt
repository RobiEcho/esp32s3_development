idf_component_register(SRCS "lvgl_port.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl BSP freertos esp_timer)
