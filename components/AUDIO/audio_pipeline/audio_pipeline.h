#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t audio_pipeline_start(BaseType_t core_id);
esp_err_t audio_pipeline_stop(void);

#endif
