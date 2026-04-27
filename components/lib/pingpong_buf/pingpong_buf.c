#include "pingpong_buf.h"
#include "esp_heap_caps.h"

pingpong_buf_t* ppb_create(size_t buf_size, uint32_t caps) {
    // 为句柄分配内存
    pingpong_buf_t* ppb = heap_caps_malloc(sizeof(pingpong_buf_t), MALLOC_CAP_8BIT);
    if (!ppb) return NULL;

    // 记录缓冲区大小
    ppb->buf_size = buf_size;
    
    // 初始化自旋锁
    ppb->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    for (int i = 0; i < 2; i++) {
        ppb->blocks[i].data = heap_caps_malloc(buf_size, caps);
        if (!ppb->blocks[i].data) {
            // 简单防错：如果分配失败，释放已分配的资源
            if (i == 1) heap_caps_free(ppb->blocks[0].data);
            heap_caps_free(ppb);
            return NULL;
        }
        ppb->blocks[i].status = PPB_IDLE;
    }
    
    return ppb;
}

void ppb_destroy(pingpong_buf_t* ppb) {
    if (!ppb) return;
    
    // 释放两个缓冲区
    for (int i = 0; i < 2; i++) {
        if (ppb->blocks[i].data) {
            heap_caps_free(ppb->blocks[i].data);
        }
    }
    
    // 释放句柄本身
    heap_caps_free(ppb);
}

void* ppb_get_idle_block(pingpong_buf_t* ppb, uint8_t *index) {
    if (!ppb) return NULL;
    void *ptr = NULL;
    
    portENTER_CRITICAL(&ppb->lock);
    for (uint8_t i = 0; i < 2; i++) {
        if (ppb->blocks[i].status == PPB_IDLE) {
            ppb->blocks[i].status = PPB_DECODING;
            *index = i;
            ptr = ppb->blocks[i].data;
            break;
        }
    }
    portEXIT_CRITICAL(&ppb->lock);
    return ptr;
}

void ppb_set_ready(pingpong_buf_t* ppb, uint8_t index) {
    if (!ppb || index >= 2) return;
    portENTER_CRITICAL(&ppb->lock);
    ppb->blocks[index].status = PPB_READY;
    portEXIT_CRITICAL(&ppb->lock);
}

void* ppb_get_ready_block(pingpong_buf_t* ppb, uint8_t *index) {
    if (!ppb) return NULL;
    void *ptr = NULL;
    
    portENTER_CRITICAL(&ppb->lock);
    for (uint8_t i = 0; i < 2; i++) {
        if (ppb->blocks[i].status == PPB_READY) {
            *index = i;
            ptr = ppb->blocks[i].data;
            break;
        }
    }
    portEXIT_CRITICAL(&ppb->lock);
    return ptr;
}

void ppb_set_displaying(pingpong_buf_t* ppb, uint8_t index) {
    if (!ppb || index >= 2) return;
    portENTER_CRITICAL(&ppb->lock);
    ppb->blocks[index].status = PPB_DISPLAYING;
    portEXIT_CRITICAL(&ppb->lock);
}

void ppb_release(pingpong_buf_t* ppb, uint8_t index, ppb_context_t context) {
    if (!ppb || index >= 2) return;
    
    if (context == PPB_CONTEXT_ISR) {
        // ISR 环境下使用专用的进入/退出宏
        portENTER_CRITICAL_ISR(&ppb->lock);
        ppb->blocks[index].status = PPB_IDLE;
        portEXIT_CRITICAL_ISR(&ppb->lock);
    } else {
        // 普通任务环境
        portENTER_CRITICAL(&ppb->lock);
        ppb->blocks[index].status = PPB_IDLE;
        portEXIT_CRITICAL(&ppb->lock);
    }
}