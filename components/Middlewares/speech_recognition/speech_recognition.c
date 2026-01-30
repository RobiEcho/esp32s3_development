#include "speech_recognition.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mn_speech_commands.h"
#include "esp_wn_iface.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "inmp441_mic.h"
#include "max98357a_amp.h"
#include "ws2812_led.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "speech_recognition";

static TaskHandle_t s_recog_task_handle = NULL;
static TaskHandle_t s_feed_task_handle = NULL;
static bool s_running = false;
static bool is_wakenet_detected = false;

// 去除前导空格
static const char *_skip_leading_space(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

// 执行语音命令
static void _execute_voice_command(const char *command)
{
    const char *cmd = _skip_leading_space(command);
    
    ESP_LOGI(TAG, "识别到命令：'%s'", cmd);
    
    if (strcmp(cmd, "kai deng") == 0) {
        ESP_LOGI(TAG, "执行：开灯");
        ws2812_led_start_rainbow();
    } else if (strcmp(cmd, "guan deng") == 0) {
        ESP_LOGI(TAG, "执行：关灯");
        ws2812_led_clear();
    } else {
        ESP_LOGW(TAG, "未识别的命令");
    }
}

// AFE和模型句柄
static const esp_afe_sr_iface_t *s_afe_handle = NULL;  // AFE接口句柄（音频前端处理）
static esp_afe_sr_data_t *s_afe_data = NULL;           // AFE实例数据
static esp_mn_iface_t *s_multinet = NULL;              // MultiNet接口句柄（命令词识别）
static model_iface_data_t *s_model_data_mn = NULL;     // MultiNet模型数据
static int s_afe_chunksize = 0;                        // AFE每次处理的音频块大小（采样点数）

// 音频缓冲区
static int32_t *s_audio_buffer_32 = NULL;
static int16_t *s_audio_buffer_16 = NULL;

static void _play_response_audio(void)
{
    ESP_LOGI(TAG, "唤醒反馈：我在！");
    //TODO:可以播放语音，但还没设计
}

static void audio_feed_task(void *arg)
{
    ESP_LOGI(TAG, "音频采集任务已启动");
    
    while (s_running) {
        size_t bytes_read = 0;
        
        // 从麦克风读取音频数据（32位）
        esp_err_t ret = inmp441_mic_read(s_audio_buffer_32, s_afe_chunksize * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read != s_afe_chunksize * sizeof(int32_t)) {
            continue;
        }
        
        // 转换为16位音频
        for (int i = 0; i < s_afe_chunksize; i++) {
            s_audio_buffer_16[i] = (int16_t)(s_audio_buffer_32[i] >> 16);
        }
        
        // 输出到扬声器（用于调试，后续扬声器专门负责回复语音）
        size_t bytes_written = 0;
        max98357a_amp_write(s_audio_buffer_16, s_afe_chunksize * sizeof(int16_t), &bytes_written, 0);
        
        // 给AFE喂数据
        s_afe_handle->feed(s_afe_data, s_audio_buffer_16);
    }
    
    ESP_LOGI(TAG, "音频采集任务已退出");
    s_feed_task_handle = NULL;
    vTaskDelete(NULL);
}

static void speech_recognition_task(void *arg)
{
    ESP_LOGI(TAG, "语音识别任务已启动");
    
    while (s_running) {
        // 获取AFE处理后的音频
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "fetch error!\n");
            continue;
        }
        
        // 检查唤醒状态
        if (res->wakeup_state == WAKENET_DETECTED) {
            _play_response_audio();
            is_wakenet_detected = true;
            ESP_LOGI(TAG, "唤醒检测: model_index=%d, word_index=%d", res->wakenet_model_index, res->wake_word_index);
        }
        
        // 如果已唤醒，进行命令词识别
        if (is_wakenet_detected) {
            esp_mn_state_t mn_state = s_multinet->detect(s_model_data_mn, res->data);
            
            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_result = s_multinet->get_results(s_model_data_mn);
                if (mn_result && mn_result->num > 0) {
                    // 执行语音命令
                    _execute_voice_command(mn_result->string);
                }
                is_wakenet_detected = false;
                
            } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
                ESP_LOGI(TAG, "命令识别超时，重新等待唤醒");
                is_wakenet_detected = false;
            }
        }
    }
    
    ESP_LOGI(TAG, "语音识别任务已退出");
    s_recog_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t speech_recognition_init(void)
{
    
    // 初始化模型列表
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "模型列表初始化失败");
        return ESP_FAIL;
    }
    
    // 打印可用的唤醒词模型
    for (int i = 0; i < models->num; i++) {
        if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {
            ESP_LOGI(TAG, "可用唤醒词模型: %s", models->model_name[i]);
        }
    }
    
    // 初始化 AFE 配置，输入格式 "M" 表示单麦克风
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) {
        ESP_LOGE(TAG, "AFE配置初始化失败");
        return ESP_FAIL;
    }
    
    if (afe_config->wakenet_model_name) {
        ESP_LOGI(TAG, "WakeNet模型: %s", afe_config->wakenet_model_name);
    }
    if (afe_config->wakenet_model_name_2) {
        ESP_LOGI(TAG, "WakeNet模型2: %s", afe_config->wakenet_model_name_2);
    }
    
    afe_config->afe_linear_gain = 3.0;  // 线性增益3倍
    afe_config->wakenet_mode = DET_MODE_95;  // 95%置信度
    
    // 打印最终配置
    // afe_config_print(afe_config);
    
    // 获取 AFE 句柄
    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (!s_afe_handle) {
        ESP_LOGE(TAG, "AFE句柄获取失败");
        afe_config_free(afe_config);
        return ESP_FAIL;
    }
    
    // 创建 AFE 实例
    s_afe_data = s_afe_handle->create_from_config(afe_config);
    if (!s_afe_data) {
        ESP_LOGE(TAG, "AFE实例创建失败");
        afe_config_free(afe_config);
        return ESP_FAIL;
    }
    
    // 释放配置
    afe_config_free(afe_config);
    
    // 分配音频缓冲区
    s_afe_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
#if CONFIG_SPIRAM
    s_audio_buffer_32 = heap_caps_malloc(s_afe_chunksize * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    s_audio_buffer_16 = heap_caps_malloc(s_afe_chunksize * sizeof(int16_t), MALLOC_CAP_SPIRAM);
#else
    s_audio_buffer_32 = heap_caps_malloc(s_afe_chunksize * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    s_audio_buffer_16 = heap_caps_malloc(s_afe_chunksize * sizeof(int16_t), MALLOC_CAP_INTERNAL);
#endif
    
    if (!s_audio_buffer_32 || !s_audio_buffer_16) {
        ESP_LOGE(TAG, "音频缓冲区分配失败");
        if (s_audio_buffer_32) {
            free(s_audio_buffer_32);
            s_audio_buffer_32 = NULL;
        }
        if (s_audio_buffer_16) {
            free(s_audio_buffer_16);
            s_audio_buffer_16 = NULL;
        }
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "音频缓冲区已分配: %d 采样点", s_afe_chunksize);
    ESP_ERROR_CHECK(inmp441_mic_init(0, s_afe_chunksize));
    ESP_ERROR_CHECK(inmp441_mic_enable());  
    ESP_ERROR_CHECK(max98357a_amp_init(0, s_afe_chunksize));
    ESP_ERROR_CHECK(max98357a_amp_enable());
    
    // 初始化MultiNet
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (!mn_name) {
        ESP_LOGE(TAG, "未找到MultiNet模型");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MultiNet模型: %s", mn_name);
    
    s_multinet = esp_mn_handle_from_name(mn_name);
    if (!s_multinet) {
        ESP_LOGE(TAG, "MultiNet句柄获取失败");
        return ESP_FAIL;
    }
    
    s_model_data_mn = s_multinet->create(mn_name, 5760);
    if (!s_model_data_mn) {
        ESP_LOGE(TAG, "MultiNet模型创建失败");
        return ESP_FAIL;
    }
    
    // 添加命令词
    esp_mn_commands_clear();
    esp_mn_commands_add(1, "kai deng");
    esp_mn_commands_add(2, "guan deng");
    esp_mn_commands_update();
    
    ESP_LOGI(TAG, "语音识别初始化完成（双任务模式）");
    return ESP_OK;
}

esp_err_t speech_recognition_start(void)
{
    if (s_recog_task_handle != NULL || s_feed_task_handle != NULL) {
        ESP_LOGW(TAG, "任务已在运行");
        return ESP_OK;
    }
    
    s_running = true;
    
    // 创建音频采集任务（CPU0）
    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_feed_task, 
        "audio_feed", 
        4096,
        NULL, 
        5,
        &s_feed_task_handle, 
        0               // CPU0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建音频采集任务失败");
        s_running = false;
        return ESP_FAIL;
    }
    
    // 创建语音识别任务（CPU1）
    ret = xTaskCreatePinnedToCore(
        speech_recognition_task, 
        "speech_recog", 
        8192,
        NULL,
        5,
        &s_recog_task_handle, 
        1               // CPU1
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建识别任务失败");
        s_running = false;
        
        // 停止已创建的音频采集任务
        if (s_feed_task_handle != NULL) {
            vTaskDelete(s_feed_task_handle);
            s_feed_task_handle = NULL;
        }
        
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "语音识别已启动（双任务模式：音频采集@CPU0 + 识别@CPU1）");
    return ESP_OK;
}

esp_err_t speech_recognition_stop(void)
{
    s_running = false;
    
    // 等待任务结束
    if (s_recog_task_handle != NULL) {
        ESP_LOGI(TAG, "等待识别任务退出...");
        while (s_recog_task_handle != NULL) vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (s_feed_task_handle != NULL) {
        ESP_LOGI(TAG, "等待音频采集任务退出...");
        while (s_feed_task_handle != NULL) vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 释放缓冲区
    if (s_audio_buffer_32) {
        free(s_audio_buffer_32);
        s_audio_buffer_32 = NULL;
    }
    if (s_audio_buffer_16) {
        free(s_audio_buffer_16);
        s_audio_buffer_16 = NULL;
    }
    
    // 清理资源
    if (s_afe_data) {
        s_afe_handle->destroy(s_afe_data);
        s_afe_data = NULL;
    }
    if (s_model_data_mn) {
        s_multinet->destroy(s_model_data_mn);
        s_model_data_mn = NULL;
    }
    
    ESP_LOGI(TAG, "语音识别已停止");
    return ESP_OK;
}
