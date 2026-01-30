#ifndef SPEECH_RECOGNITION_H
#define SPEECH_RECOGNITION_H

#include "esp_err.h"

esp_err_t speech_recognition_init(void);
esp_err_t speech_recognition_start(void);
esp_err_t speech_recognition_stop(void);

#endif /* SPEECH_RECOGNITION_H */
