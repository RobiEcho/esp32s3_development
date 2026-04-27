#include "ring_buf.h"
#include "esp_heap_caps.h"
#include <string.h>

ring_buf_t* ring_buf_create(size_t size, uint32_t caps)
{
    ring_buf_t* rb = heap_caps_malloc(sizeof(ring_buf_t), MALLOC_CAP_8BIT);
    if (!rb) return NULL;

    rb->buf = heap_caps_malloc(size, caps);
    if (!rb->buf) {
        heap_caps_free(rb);
        return NULL;
    }

    rb->size = size;
    rb->write_pos = 0;
    rb->read_pos = 0;
    rb->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return rb;
}

void ring_buf_destroy(ring_buf_t* rb)
{
    if (rb) {
        if (rb->buf) heap_caps_free(rb->buf);
        heap_caps_free(rb);
    }
}

esp_err_t ring_buf_write(ring_buf_t* rb, const uint8_t *data, size_t len)
{
    if (!rb || !data || len == 0) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&rb->lock);
    
    // 检查空间是否足够
    size_t used = (rb->write_pos - rb->read_pos + rb->size) % rb->size;
    size_t free_size = rb->size - used - 1;
    
    if (free_size < len) {
        portEXIT_CRITICAL(&rb->lock);
        return ESP_ERR_NO_MEM;
    }

    size_t write_pos = rb->write_pos;
    
    // 在持有锁的情况下执行 memcpy，确保数据一致性
    size_t first_part = rb->size - write_pos;
    if (len <= first_part) {
        memcpy(rb->buf + write_pos, data, len);
    } else {
        memcpy(rb->buf + write_pos, data, first_part);
        memcpy(rb->buf, data + first_part, len - first_part);
    }

    rb->write_pos = (rb->write_pos + len) % rb->size;
    portEXIT_CRITICAL(&rb->lock);
    
    return ESP_OK;
}

esp_err_t ring_buf_read(ring_buf_t* rb, uint8_t *out_data, size_t offset, size_t len)
{
    if (!rb || !out_data || len == 0) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&rb->lock);
    
    // 检查请求的数据是否在有效范围内
    size_t used = (rb->write_pos - rb->read_pos + rb->size) % rb->size;
    size_t offset_from_read = (offset - rb->read_pos + rb->size) % rb->size;
    
    if (offset_from_read + len > used) {
        portEXIT_CRITICAL(&rb->lock);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 在持有锁的情况下执行 memcpy，确保读取的数据不会被覆盖
    size_t read_offset = offset % rb->size;
    size_t first_part = rb->size - read_offset;
    if (len <= first_part) {
        memcpy(out_data, rb->buf + read_offset, len);
    } else {
        memcpy(out_data, rb->buf + read_offset, first_part);
        memcpy(out_data + first_part, rb->buf, len - first_part);
    }

    portEXIT_CRITICAL(&rb->lock);
    return ESP_OK;
}

size_t ring_buf_get_write_pos(ring_buf_t* rb)
{
    if (!rb) return 0;
    
    portENTER_CRITICAL(&rb->lock);
    size_t pos = rb->write_pos;
    portEXIT_CRITICAL(&rb->lock);
    
    return pos;
}

void ring_buf_finish_frame(ring_buf_t* rb, size_t next_frame_start)
{
    if (!rb) return;
    
    portENTER_CRITICAL(&rb->lock);
    
    // 验证 next_frame_start 在有效范围内
    size_t normalized_next = next_frame_start % rb->size;
    size_t used = (rb->write_pos - rb->read_pos + rb->size) % rb->size;
    size_t offset_from_read = (normalized_next - rb->read_pos + rb->size) % rb->size;
    
    if (offset_from_read <= used) {
        rb->read_pos = normalized_next;
    }
    
    portEXIT_CRITICAL(&rb->lock);
}

size_t ring_buf_get_free_size(ring_buf_t* rb)
{
    if (!rb) return 0;
    
    portENTER_CRITICAL(&rb->lock);
    size_t used = (rb->write_pos - rb->read_pos + rb->size) % rb->size;
    size_t free_size = rb->size - used - 1;
    portEXIT_CRITICAL(&rb->lock);
    
    return free_size;
}

size_t ring_buf_get_used_size(ring_buf_t* rb)
{
    if (!rb) return 0;
    
    portENTER_CRITICAL(&rb->lock);
    size_t used = (rb->write_pos - rb->read_pos + rb->size) % rb->size;
    portEXIT_CRITICAL(&rb->lock);
    
    return used;
}