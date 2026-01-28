#ifndef WS2812_LED_H
#define WS2812_LED_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t ws2812_led_init(void);
esp_err_t ws2812_led_set_color(uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_led_set_brightness(uint8_t brightness);
esp_err_t ws2812_led_start_rainbow(void);
esp_err_t ws2812_led_clear(void);

#endif /* WS2812_LED_H */
