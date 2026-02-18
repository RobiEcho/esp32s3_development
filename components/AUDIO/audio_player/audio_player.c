#include "audio_player.h"
#include "audio_partition_map.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "max98357a_amp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "audio_player";
static const esp_partition_t *s_audio_partition = NULL;
static volatile bool s_is_playing = false;
static audio_index_header_t s_index_header = {0};

#define AUDIO_CHUNK_SIZE 4096  // 每次读取4KB

esp_err_t audio_player_init(void)
{
    // 查找音频分区
    s_audio_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        "audio"
    );
    
    if (!s_audio_partition) {
        ESP_LOGE(TAG, "未找到音频分区");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "音频分区已找到: 地址=0x%lx, 大小=%lu KB",
             (unsigned long)s_audio_partition->address, 
             (unsigned long)(s_audio_partition->size / 1024));
    
    // 读取并验证索引表头
    esp_err_t ret = esp_partition_read(s_audio_partition, 0, &s_index_header, sizeof(audio_index_header_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取索引表头失败");
        return ret;
    }
    
    // 验证魔数
    if (s_index_header.magic != AUDIO_INDEX_MAGIC) {
        ESP_LOGE(TAG, "无效的索引表魔数: 0x%08lx (期望: 0x%08x)", 
                 (unsigned long)s_index_header.magic, AUDIO_INDEX_MAGIC);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 验证条目数量
    if (s_index_header.count > AUDIO_TYPE_MAX) {
        ESP_LOGE(TAG, "索引表条目数量异常: %lu > %d", 
                 (unsigned long)s_index_header.count, AUDIO_TYPE_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "音频播放器初始化完成: 音频数=%lu",
             (unsigned long)s_index_header.count);
    
    s_is_playing = false;
    
    return ESP_OK;
}

static esp_err_t audio_player_read_index(audio_type_t type, audio_index_entry_t *entry)
{
    if (!s_audio_partition) {
        ESP_LOGE(TAG, "音频分区未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (type >= AUDIO_TYPE_MAX) {
        ESP_LOGE(TAG, "无效的音频类型: %d", type);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (type >= s_index_header.count) {
        ESP_LOGE(TAG, "音频类型超出范围: %d >= %lu", type, (unsigned long)s_index_header.count);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 计算条目偏移: 头部(16字节) + 条目索引 * 条目大小(16字节)
    uint32_t entry_offset = sizeof(audio_index_header_t) + type * sizeof(audio_index_entry_t);
    
    esp_err_t ret = esp_partition_read(s_audio_partition, entry_offset, entry, sizeof(audio_index_entry_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取索引表条目失败");
        return ret;
    }
    
    // 验证条目数据
    if (entry->size == 0 || entry->size > MAX_AUDIO_SIZE) {
        ESP_LOGE(TAG, "音频大小异常: %lu 字节", (unsigned long)entry->size);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (entry->offset + entry->size > s_audio_partition->size) {
        ESP_LOGE(TAG, "音频数据超出分区范围: offset=%lu, size=%lu", 
                 (unsigned long)entry->offset, (unsigned long)entry->size);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

esp_err_t audio_player_get_info(audio_type_t type, uint32_t *size, uint32_t *sample_rate)
{
    if (!size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    audio_index_entry_t entry;
    esp_err_t ret = audio_player_read_index(type, &entry);
    
    if (ret == ESP_OK) {
        *size = entry.size;
        if (sample_rate) {
            *sample_rate = entry.sample_rate;
        }
    }
    
    return ret;
}

esp_err_t audio_player_play(audio_type_t type)
{
    if (!s_audio_partition) {
        ESP_LOGE(TAG, "音频分区未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_is_playing) {
        ESP_LOGW(TAG, "已有音频正在播放");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 读取索引表条目
    audio_index_entry_t entry;
    esp_err_t ret = audio_player_read_index(type, &entry);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGI(TAG, "开始播放音频: type=%d, offset=%lu, size=%lu, rate=%lu",
             type, (unsigned long)entry.offset, (unsigned long)entry.size, 
             (unsigned long)entry.sample_rate);
    
    s_is_playing = true;
    
    // 分配音频缓冲区(使用PSRAM)
    int16_t *buffer = heap_caps_malloc(AUDIO_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        ESP_LOGE(TAG, "内存分配失败，需要 %d 字节", AUDIO_CHUNK_SIZE);
        s_is_playing = false;
        return ESP_ERR_NO_MEM;
    }
    
    // 分块读取并播放
    uint32_t remaining = entry.size;
    uint32_t current_offset = entry.offset;
    
    while (remaining > 0 && s_is_playing) {
        // 计算本次读取大小
        size_t chunk_size = (remaining > AUDIO_CHUNK_SIZE) ? AUDIO_CHUNK_SIZE : remaining;
        
        // 从Flash读取音频数据
        ret = esp_partition_read(s_audio_partition, current_offset, buffer, chunk_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "读取音频数据失败: offset=%lu", (unsigned long)current_offset);
            break;
        }
        
        // 播放音频数据(阻塞等待DMA完成)
        size_t bytes_written = 0;
        ret = max98357a_amp_write(buffer, chunk_size, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "播放音频失败");
            break;
        }
        
        // 更新偏移和剩余大小
        current_offset += chunk_size;
        remaining -= chunk_size;
    }
    
    // 释放缓冲区
    free(buffer);
    
    s_is_playing = false;
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "音频播放完成 (%lu 字节)", (unsigned long)entry.size);
    } else {
        ESP_LOGE(TAG, "音频播放失败");
    }
    
    return ret;
}

esp_err_t audio_player_stop(void)
{
    if (!s_is_playing) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "停止播放");
    s_is_playing = false;
    
    // 等待播放任务退出
    vTaskDelay(pdMS_TO_TICKS(100));
    
    return ESP_OK;
}
