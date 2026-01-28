#ifndef MAX98357A_AMP_H
#define MAX98357A_AMP_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t max98357a_amp_init(uint32_t dma_desc_num, uint32_t dma_frame_num);
esp_err_t max98357a_amp_enable(void);
esp_err_t max98357a_amp_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms);
esp_err_t max98357a_amp_disable(void);
esp_err_t max98357a_amp_deinit(void);

#endif
