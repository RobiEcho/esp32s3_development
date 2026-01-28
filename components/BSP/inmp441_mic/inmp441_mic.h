#ifndef INMP441_MIC_H
#define INMP441_MIC_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t inmp441_mic_init(uint32_t dma_desc_num, uint32_t dma_frame_num);
esp_err_t inmp441_mic_enable(void);
esp_err_t inmp441_mic_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);
esp_err_t inmp441_mic_disable(void);
esp_err_t inmp441_mic_deinit(void);

#endif /* INMP441_MIC_H */
