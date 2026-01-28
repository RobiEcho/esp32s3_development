#include "ws2812_led.h"
#include "led_strip.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "WS2812";

#define WS2812_GPIO                (48)                // LED GPIO 引脚
#define WS2812_RMT_RES_HZ          (10 * 1000 * 1000)  // RMT 时钟频率 10MHz
#define WS2812_DEFAULT_BRIGHTNESS  (15)                // 默认亮度 (0-255)
#define WS2812_RAINBOW_PERIOD_MS   (20)                // 彩虹更新周期 (ms)

static led_strip_handle_t s_led_strip = NULL;
static esp_timer_handle_t s_rainbow_timer = NULL;
static uint16_t s_hue = 0;
static uint8_t s_brightness = WS2812_DEFAULT_BRIGHTNESS;

static void _hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h %= 360;
    uint8_t region = h / 60;
    uint8_t remainder = (h - (region * 60)) * 255 / 60;

    uint8_t p = (v * (255 - s)) / 255;
    uint8_t q = (v * (255 - ((s * remainder) / 255))) / 255;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static void _rainbow_timer_callback(void *arg)
{
    uint8_t r, g, b;
    _hsv_to_rgb(s_hue, 255, s_brightness, &r, &g, &b);
    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    led_strip_refresh(s_led_strip);
    
    s_hue = (s_hue + 2) % 360;
}

esp_err_t ws2812_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = WS2812_GPIO,
        .max_leds = 1,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = WS2812_RMT_RES_HZ,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led_strip));
    led_strip_clear(s_led_strip);

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t ws2812_led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led_strip) return ESP_ERR_INVALID_STATE;
    
    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    return led_strip_refresh(s_led_strip);
}

esp_err_t ws2812_led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
    return ESP_OK;
}

esp_err_t ws2812_led_start_rainbow(void)
{
    if (s_rainbow_timer != NULL) {
        return ESP_OK;
    }
    
    const esp_timer_create_args_t timer_args = {
        .callback = _rainbow_timer_callback,
        .name = "ws2812_rainbow_timer"
    };
    
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_rainbow_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_rainbow_timer, WS2812_RAINBOW_PERIOD_MS * 1000));
    
    return ESP_OK;
}

esp_err_t ws2812_led_clear(void)
{
    if (s_rainbow_timer != NULL) {
        esp_timer_stop(s_rainbow_timer);
        esp_timer_delete(s_rainbow_timer);
        s_rainbow_timer = NULL;
        s_hue = 0;
    }
    
    led_strip_clear(s_led_strip);
    return ESP_OK;
}
