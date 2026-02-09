#include "st7789_lcd.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ST7789";

#define ST7789_SPI_HOST              SPI3_HOST              // SPI 外设选择
#define ST7789_SPI_MODE              3                      // SPI 模式 3
#define ST7789_SPI_MOSI_PIN          11                     // MOSI 引脚 (IOMUX)
#define ST7789_SPI_SCLK_PIN          12                     // SCLK 引脚 (IOMUX)
#define ST7789_RES_PIN               19                     // 复位引脚
#define ST7789_DC_PIN                20                     // 数据/命令引脚
#define ST7789_CS_PIN                -1                     // 片选引脚 (未使用)
#define LCD_PIXEL_CLOCK_HZ           (40 * 1000 * 1000)     // SPI 时钟频率
#define LCD_H_RES                    240                    // 水平分辨率
#define LCD_V_RES                    240                    // 垂直分辨率

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;

esp_err_t st7789_lcd_init(void)
{
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = ST7789_SPI_SCLK_PIN,
        .mosi_io_num = ST7789_SPI_MOSI_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (LCD_H_RES * LCD_V_RES / 8) * sizeof(uint16_t), // SPI DMA 最大传输大小（1/8 屏幕 = 30 行 = 14.4KB）
    };
    ESP_ERROR_CHECK(spi_bus_initialize(ST7789_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = ST7789_DC_PIN,
        .cs_gpio_num = ST7789_CS_PIN,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = ST7789_SPI_MODE,
        .trans_queue_depth = 10,        // SPI 传输队列深度（可排队的传输事务数量）
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ST7789_SPI_HOST, &io_cfg, &s_io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = ST7789_RES_PIN,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io_handle, &panel_cfg, &s_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));               // 复位 LCD
    vTaskDelay(pdMS_TO_TICKS(120));     
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));                // 初始化 LCD 寄存器
    vTaskDelay(pdMS_TO_TICKS(20)); 
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));  // 反转颜色
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, true, true));  // 镜像显示（水平+垂直）
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, 0, 80));      // 设置显示偏移
    vTaskDelay(pdMS_TO_TICKS(10));
    st7789_lcd_clear_screen(0x0000);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));   // 打开显示 

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t st7789_lcd_register_trans_done_cb(esp_lcd_panel_io_color_trans_done_cb_t cb, void *user_ctx)
{
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = cb,
    };
    return esp_lcd_panel_io_register_event_callbacks(s_io_handle, &cbs, user_ctx);
}

void st7789_lcd_draw_bitmap(int x1, int y1, int x2, int y2, void *color_data)
{
    // ST7789 需要的大端序，传入的数据要先交换高低位，这里是不做字节序交换逻辑
    esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2, y2, color_data);
}

void st7789_lcd_clear_screen(uint16_t color)
{
    uint16_t *buffer = heap_caps_malloc(LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buffer) return;
    
    // ST7789 字节序：需要交换高低字节
    uint16_t swapped_color = (color >> 8) | (color << 8);
    for (int i = 0; i < LCD_H_RES; i++) {
        buffer[i] = swapped_color;
    }
    
    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, LCD_H_RES, y + 1, buffer);
    }
    
    free(buffer);
}

int st7789_lcd_get_h_res(void)
{
    return LCD_H_RES;
}

int st7789_lcd_get_v_res(void)
{
    return LCD_V_RES;
}
