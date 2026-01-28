#ifndef ST7789_LCD_H
#define ST7789_LCD_H

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include <stdint.h>

esp_err_t st7789_lcd_init(void);
esp_err_t st7789_lcd_register_trans_done_cb(esp_lcd_panel_io_color_trans_done_cb_t cb, void *user_ctx);
void st7789_lcd_draw_bitmap(int x1, int y1, int x2, int y2, void *color_data);
void st7789_lcd_clear_screen(uint16_t color);
int st7789_lcd_get_h_res(void);
int st7789_lcd_get_v_res(void);

#endif /* ST7789_LCD_H */
