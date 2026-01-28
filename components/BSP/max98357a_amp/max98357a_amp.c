#include "max98357a_amp.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX98357A_AMP";

#define AUDIO_SAMPLE_RATE               16000
#define MAX98357A_AMP_BITS_PER_SAMPLE   16
#define MAX98357A_AMP_CHANNELS          1
#define I2S_WS_PIN      16
#define I2S_BCK_PIN     15
#define I2S_DO_PIN      7

static i2s_chan_handle_t s_tx_handle = NULL;

esp_err_t max98357a_amp_init(uint32_t dma_desc_num, uint32_t dma_frame_num)
{
    // 如果传入 0，使用默认值
    if (dma_desc_num == 0) {
        dma_desc_num = 6;
    }
    if (dma_frame_num == 0) {
        dma_frame_num = 48;
    }
    
    // 配置 I2S 通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = dma_desc_num;
    chan_cfg.dma_frame_num = dma_frame_num;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL));
    
    // 配置标准 I2S 模式（16 位，单声道左声道）
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DO_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &std_cfg));
    
    ESP_LOGI(TAG, "初始化完成 (DMA: %lux%lu=%lu 采样点)", 
             dma_desc_num, dma_frame_num, dma_desc_num * dma_frame_num);
    
    return ESP_OK;
}

esp_err_t max98357a_amp_enable(void)
{
    if (s_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));
    vTaskDelay(pdMS_TO_TICKS(100));  // 等待 I2S 稳定
    
    ESP_LOGI(TAG, "I2S 通道已启用");
    
    return ESP_OK;
}

esp_err_t max98357a_amp_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms)
{
    if (s_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return i2s_channel_write(s_tx_handle, buffer, buffer_size, bytes_written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t max98357a_amp_disable(void)
{
    if (s_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_ERROR_CHECK(i2s_channel_disable(s_tx_handle));
    ESP_LOGI(TAG, "I2S 通道已禁用");
    
    return ESP_OK;
}

esp_err_t max98357a_amp_deinit(void)
{
    if (s_tx_handle != NULL) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    
    return ESP_OK;
}
