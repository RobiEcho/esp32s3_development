#include "lvgl_port.h"
#include "st7789_lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "LVGL";

#define LVGL_DRAW_BUF_FRACTION       8          // 绘图缓冲区大小（屏幕的 1/N）
#define LVGL_TICK_PERIOD_MS          2          // LVGL 时钟周期 (ms)
#define LVGL_TASK_STACK_SIZE         (4 * 1024) // LVGL 任务栈大小
#define LVGL_TASK_PRIORITY           2          // LVGL 任务优先级

static SemaphoreHandle_t s_lvgl_mux = NULL;
static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_drv_t s_disp_drv;

// DMA 传输完成回调（通知 LVGL 刷新已完成）
static bool _lcd_dma_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_flush_ready(&s_disp_drv);
    return false;
}

// LVGL 刷新回调
static void _flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    st7789_lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}

// LVGL 时钟回调
static void _tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// LVGL 任务
static void _task(void *arg)
{
    ESP_LOGI(TAG, "Task started");
    uint32_t delay_ms = 10;
    
    while (1) {
        if (xSemaphoreTake(s_lvgl_mux, portMAX_DELAY) == pdTRUE) {
            delay_ms = lv_timer_handler();
            xSemaphoreGive(s_lvgl_mux);
        }
        if (delay_ms > 500) delay_ms = 500;
        else if (delay_ms < 10) delay_ms = 10;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

lv_disp_t *lvgl_port_init(void)
{
    // 初始化 LCD
    st7789_lcd_init();

    lv_init();

    int hor_res = st7789_lcd_get_h_res();
    int ver_res = st7789_lcd_get_v_res();
    int buf_lines = ver_res / LVGL_DRAW_BUF_FRACTION;  // 计算缓冲区行数

    // 分配绘图缓冲区
#if CONFIG_SPIRAM
    lv_color_t *buf1 = heap_caps_malloc(hor_res * buf_lines * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(hor_res * buf_lines * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
#else
    lv_color_t *buf1 = heap_caps_malloc(hor_res * buf_lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = heap_caps_malloc(hor_res * buf_lines * sizeof(lv_color_t), MALLOC_CAP_DMA);
#endif
    
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        if (buf1) free(buf1);
        if (buf2) free(buf2);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Buffer: 1/%d screen, %d bytes per block", 
            LVGL_DRAW_BUF_FRACTION, hor_res * buf_lines * sizeof(lv_color_t));
    
    lv_disp_draw_buf_init(&s_disp_buf, buf1, buf2, hor_res * buf_lines);

    // 注册显示驱动
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = hor_res;
    s_disp_drv.ver_res = ver_res;
    s_disp_drv.flush_cb = _flush_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    lv_disp_t *disp = lv_disp_drv_register(&s_disp_drv);

    // 注册 DMA 完成回调
    st7789_lcd_register_trans_done_cb(_lcd_dma_trans_done_cb, NULL);

    // 创建 LVGL 时钟定时器
    const esp_timer_create_args_t timer_args = {
        .callback = _tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000));

    // 创建 LVGL 互斥锁
    s_lvgl_mux = xSemaphoreCreateMutex();
    if (s_lvgl_mux == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return NULL;
    }

    ESP_LOGI(TAG, "Initialized");
    return disp;
}

esp_err_t lvgl_port_start_task(BaseType_t core_id)
{
    if (s_lvgl_mux == NULL) {
        ESP_LOGE(TAG, "LVGL not initialized, call lvgl_port_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t ret;
    if (core_id == tskNO_AFFINITY) {
        ret = xTaskCreate(_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    } else {
        ret = xTaskCreatePinnedToCore(_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, core_id);
    }
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "LVGL task started (CPU: %s)", 
             core_id == tskNO_AFFINITY ? "any" : (core_id == 0 ? "0" : "1"));
    return ESP_OK;
}

bool lvgl_port_lock_mutex(uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_lvgl_mux, ticks) == pdTRUE;
}

void lvgl_port_unlock_mutex(void)
{
    xSemaphoreGive(s_lvgl_mux);
}
