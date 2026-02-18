#ifndef __VIDEO_DECODE_H__
#define __VIDEO_DECODE_H__

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t width;
    uint16_t height;
    size_t output_len;
} video_frame_info_t;

esp_err_t video_decode_init(void);
esp_err_t video_decode_push_data(const uint8_t *data, size_t len);
esp_err_t video_decode_get_frame(uint8_t **out_data, size_t *out_len);
esp_err_t video_decode_release_frame(size_t frame_len);
esp_err_t video_decode_process(const uint8_t *jpeg_data, size_t jpeg_len, 
                                uint16_t *out_rgb565, size_t out_len, 
                                video_frame_info_t *frame_info);

#endif /* __VIDEO_DECODE_H__ */
