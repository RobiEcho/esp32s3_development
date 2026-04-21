#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "lvgl.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdbool.h>

lv_disp_t *lvgl_port_init(void);
esp_err_t lvgl_port_start_task(BaseType_t core_id);
bool lvgl_port_lock_mutex(uint32_t timeout_ms);
void lvgl_port_unlock_mutex(void);

#endif
