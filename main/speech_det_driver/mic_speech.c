#include "mic_speech.h"

// 语音识别相关头文件（当前回声测试不需要）
// #include "esp_wn_iface.h"
// #include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
// #include "esp_mn_iface.h"
// #include "esp_mn_models.h"
// #include "model_path.h"
// #include "esp_process_sdkconfig.h"

#include "bsp_board.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "websocket.h"

static const char *TAG = "App/Speech";

// ===============================================
// 可调参数配置区域 - 根据需要修改这些值
// ===============================================
#define LOOPBACK_LINEAR_GAIN        10.0f   // AFE线性增益 (范围: 0.1~10.0, 默认1.0)
                                            // 增大此值可提高麦克风灵敏度（远距离收音）
                                            // 建议值: 2.0~5.0

#define LOOPBACK_AGC_COMPRESSION_GAIN  15   // AGC压缩增益 (dB, 默认9)
                                             // 增大此值可增强小音量信号
                                             // 建议值: 9~20

#define LOOPBACK_AGC_TARGET_LEVEL      6    // AGC目标电平 (-dBFS, 默认3)
                                             // 降低此值可提高整体音量
                                             // 建议值: 1~6

#define LOOPBACK_OUTPUT_AMPLIFY        3    // 输出音量放大倍数 (降低以避免刺耳)
                                             // 建议值: 1~3 (过大会导致削波失真)

#define LOOPBACK_AEC_FILTER_LENGTH     4    // AEC滤波器长度 (默认4)
                                             // 值越大回声消除效果越好，但CPU占用越高
                                             // 可选值: 2, 4, 8

// ===============================================
// 人声检测与音质优化配置
// ===============================================
#define LOOPBACK_ENABLE_VAD_FILTER  true    // 启用VAD人声过滤（只播放人声）
                                             // true: 只播放检测到人声时的音频
                                             // false: 持续播放所有音频

#define LOOPBACK_ENABLE_SOFT_LIMITER true   // 启用软限幅器（防止刺耳）
                                             // 防止音频削波导致的刺耳失真

#define LOOPBACK_LIMITER_THRESHOLD  28000   // 软限幅器阈值 (建议: 26000~30000)
                                             // 超过此值的音频会被平滑压缩
                                             // 降低此值可以更有效防止刺耳声音

// ===============================================
// WebSocket 音频流传输配置
// ===============================================
#define ENABLE_WEBSOCKET_STREAM     true    // 启用 WebSocket 音频流传输
                                             // true: 将音频数据发送到 WebSocket 服务器
                                             // false: 仅本地播放
                                             // 注意：需要先配置 websocket.h 中的 WiFi 和服务器地址


static esp_afe_sr_iface_t *afe_handle = NULL;             // AFE句柄
static volatile int task_flag = 0;                         // 任务运行标志


// ===============================================
// 音频回声测试功能（使用AFE算法，关闭语音识别）
// ===============================================

// 软限幅器 - 防止削波失真（刺耳声音）
static inline int16_t soft_limiter(int32_t sample) {
#if LOOPBACK_ENABLE_SOFT_LIMITER
    if (sample > LOOPBACK_LIMITER_THRESHOLD) {
        // 平滑压缩超过阈值的部分
        int32_t excess = sample - LOOPBACK_LIMITER_THRESHOLD;
        sample = LOOPBACK_LIMITER_THRESHOLD + (excess / 3);  // 压缩比 3:1
        if (sample > 32767) sample = 32767;
    } else if (sample < -LOOPBACK_LIMITER_THRESHOLD) {
        int32_t excess = sample + LOOPBACK_LIMITER_THRESHOLD;
        sample = -LOOPBACK_LIMITER_THRESHOLD + (excess / 3);
        if (sample < -32768) sample = -32768;
    }
#else
    // 硬限幅
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
#endif
    return (int16_t)sample;
}

// 音频采集任务（送入AFE处理）
void loopback_feed_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int nch = afe_handle->get_feed_channel_num(afe_data);
    int feed_channel = esp_get_feed_channel();
    
    ESP_LOGI(TAG, "=== Loopback Feed Task Started ===");
    ESP_LOGI(TAG, "Chunk size: %d samples, Channels: %d", audio_chunksize, nch);
    
    assert(nch == feed_channel);
    int16_t *i2s_buff = heap_caps_malloc(audio_chunksize * sizeof(int16_t) * feed_channel, MALLOC_CAP_SPIRAM);
    assert(i2s_buff);

    esp_task_wdt_add(NULL);
    int feed_count = 0;
    
    while (task_flag) {
        // 从麦克风采集音频
        esp_get_feed_data(true, i2s_buff, audio_chunksize * sizeof(int16_t) * feed_channel);
        
        feed_count++;
        if (feed_count % 100 == 0) {
            ESP_LOGI(TAG, "[FEED] Count:%d, Samples: %d, %d, %d, %d", 
                     feed_count, i2s_buff[0], i2s_buff[1], i2s_buff[2], i2s_buff[3]);
        }
        
        // 送入AFE进行处理（AEC、SE、NS、AGC等）
        afe_handle->feed(afe_data, i2s_buff);
        esp_task_wdt_reset();
    }
    
    if (i2s_buff) {
        free(i2s_buff);
    }
    ESP_LOGI(TAG, "Loopback feed task exit");
    vTaskDelete(NULL);
}

// 音频播放任务（从AFE获取处理后的音频并播放）
void loopback_detect_task(void *arg)
{
    esp_afe_sr_data_t *afe_data = arg;
    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data);
    
    ESP_LOGI(TAG, "=== Loopback Detect Task Started ===");
    ESP_LOGI(TAG, "AFE chunk size: %d samples", afe_chunksize);
    ESP_LOGI(TAG, "VAD Filter: %s", LOOPBACK_ENABLE_VAD_FILTER ? "ENABLED (只播放人声)" : "DISABLED");
    ESP_LOGI(TAG, "Soft Limiter: %s (防止刺耳)", LOOPBACK_ENABLE_SOFT_LIMITER ? "ENABLED" : "DISABLED");
    
    // 分配立体声输出缓冲区
    int16_t *stereo_buffer = heap_caps_malloc(afe_chunksize * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    assert(stereo_buffer);
    
    esp_task_wdt_add(NULL);
    int fetch_count = 0;
    int play_success = 0;
    int play_failed = 0;
    int vad_detected_count = 0;
    int vad_silence_count = 0;
    int16_t max_sample = 0;

    while (task_flag) {
        // 从AFE获取处理后的音频
        afe_fetch_result_t* res = afe_handle->fetch(afe_data); 
        
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "AFE fetch error!");
            break;
        }

        // 检查是否有有效数据
        if (res->data != NULL && res->data_size > 0) {
            fetch_count++;
            
            // VAD人声检测 (vad_state_t: VAD_SILENCE=0, VAD_SPEECH=1)
            bool is_voice_detected = (res->vad_state == VAD_SPEECH);
            if (is_voice_detected) {
                vad_detected_count++;
            } else {
                vad_silence_count++;
            }
            
            // 如果启用VAD过滤，且未检测到人声，则跳过播放
            #if LOOPBACK_ENABLE_VAD_FILTER
            if (!is_voice_detected) {
                esp_task_wdt_reset();
                continue;  // 跳过非人声音频
            }
            #endif
            
            // 计算最大采样值
            max_sample = 0;
            int sample_count = res->data_size / sizeof(int16_t);
            for (int i = 0; i < sample_count; i++) {
                int16_t abs_val = res->data[i] > 0 ? res->data[i] : -res->data[i];
                if (abs_val > max_sample) {
                    max_sample = abs_val;
                }
            }
            
            // 每100次打印一次状态
            if (fetch_count % 100 == 0) {
                ESP_LOGI(TAG, "[PLAY] Count:%d, VAD:语音%d/静音%d, Vol:%.1f dB, Max:%d, OK:%d, Fail:%d", 
                         fetch_count, vad_detected_count, vad_silence_count,
                         res->data_volume, max_sample, play_success, play_failed);
            }
            
            // 将AFE输出的单声道转换为双声道，并应用软限幅器
            int output_samples = res->data_size / sizeof(int16_t);
            for (int i = 0; i < output_samples; i++) {
                int16_t sample = res->data[i];
                
                // 放大音量
                int32_t amplified = (int32_t)sample * LOOPBACK_OUTPUT_AMPLIFY;
                
                // 应用软限幅器（防止刺耳失真）
                int16_t limited_sample = soft_limiter(amplified);
                
                // 双声道输出
                stereo_buffer[i * 2 + 0] = limited_sample;  // 左声道
                stereo_buffer[i * 2 + 1] = limited_sample;  // 右声道
            }
            
            // 播放到扬声器
            esp_err_t ret = esp_audio_play(stereo_buffer, output_samples * 2 * sizeof(int16_t), pdMS_TO_TICKS(200));
            
            if (ret == ESP_OK) {
                play_success++;
            } else {
                play_failed++;
                if (play_failed % 10 == 1) {
                    ESP_LOGE(TAG, "Play failed: %d", ret);
                }
            }
            
            // WebSocket 音频流传输
            #if ENABLE_WEBSOCKET_STREAM
            if (websocket_is_connected()) {
                // 发送原始单声道音频数据（AFE处理后的）
                websocket_send_audio(res->data, res->data_size);
            }
            #endif
        }
        
        esp_task_wdt_reset();
    }

    if (stereo_buffer) {
        free(stereo_buffer);
    }
    
    ESP_LOGI(TAG, "Loopback detect exit - Total:%d, Voice:%d, Silence:%d, Play OK:%d, Failed:%d", 
             fetch_count, vad_detected_count, vad_silence_count, play_success, play_failed);
    vTaskDelete(NULL);
}

// 初始化音频回声测试（使用AFE算法）
void Audio_Loopback_AFE_Init(void)
{
    ESP_LOGI(TAG, "\n========== Audio Loopback with AFE Init ==========");
    
    // 创建AFE配置（不加载语音识别模型）
    // 使用NULL作为models参数，这样不会启用WakeNet
    afe_config_t *afe_config = afe_config_init(
        esp_get_input_format(),  // 获取输入格式 "MMNR"等
        NULL,                     // 不使用语音识别模型
        AFE_TYPE_SR,             // 语音识别类型（但不启用识别）
        AFE_MODE_LOW_COST        // 低成本模式
    );
    
    // ===============================================
    // 启用所有音频处理算法
    // ===============================================
    afe_config->aec_init = true;      //  回声消除
    afe_config->se_init = true;       // 语音增强/波束成形
    afe_config->ns_init = true;       //  噪声抑制
    afe_config->vad_init = true;      //  语音活动检测
    afe_config->agc_init = true;      //  自动增益控制
    afe_config->wakenet_init = false; //  关闭唤醒词识别
    
    // ===============================================
    // 关键参数配置 - 提高麦克风灵敏度和音量
    // ===============================================
    
    // 1. AFE线性增益 - 影响麦克风收音距离
    afe_config->afe_linear_gain = LOOPBACK_LINEAR_GAIN;
    ESP_LOGI(TAG, "AFE Linear Gain: %.1f (提高麦克风灵敏度)", afe_config->afe_linear_gain);
    
    // 2. AGC自动增益控制参数 - 增强远距离收音
    afe_config->agc_compression_gain_db = LOOPBACK_AGC_COMPRESSION_GAIN;
    afe_config->agc_target_level_dbfs = LOOPBACK_AGC_TARGET_LEVEL;
    ESP_LOGI(TAG, "AGC: Compression=%d dB, Target=-%d dBFS", 
             afe_config->agc_compression_gain_db, 
             afe_config->agc_target_level_dbfs);
    
    // 3. AEC回声消除滤波器长度
    afe_config->aec_filter_length = LOOPBACK_AEC_FILTER_LENGTH;
    ESP_LOGI(TAG, "AEC Filter Length: %d", afe_config->aec_filter_length);
    
    // 4. VAD灵敏度设置
    afe_config->vad_mode = VAD_MODE_3;  // 模式3，较高灵敏度
                                         // 可选: VAD_MODE_0~4 (数字越大越灵敏)
    
    ESP_LOGI(TAG, "AFE Config:");
    ESP_LOGI(TAG, "   AEC (Acoustic Echo Cancellation)");
    ESP_LOGI(TAG, "   SE (Speech Enhancement)");
    ESP_LOGI(TAG, "   NS (Noise Suppression)");
    ESP_LOGI(TAG, "   VAD (Voice Activity Detection) - Mode: %d", afe_config->vad_mode);
    ESP_LOGI(TAG, "   AGC (Automatic Gain Control)");
    ESP_LOGI(TAG, "   WakeNet (Disabled)");
    ESP_LOGI(TAG, "   MultiNet (Disabled)");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Audio Processing:");
    ESP_LOGI(TAG, "   Output Amplification: %dx", LOOPBACK_OUTPUT_AMPLIFY);
    ESP_LOGI(TAG, "   VAD Filter: %s (过滤非人声噪音)", LOOPBACK_ENABLE_VAD_FILTER ? "启用" : "禁用");
    ESP_LOGI(TAG, "   Soft Limiter: %s (防止刺耳失真)", LOOPBACK_ENABLE_SOFT_LIMITER ? "启用" : "禁用");
    if (LOOPBACK_ENABLE_SOFT_LIMITER) {
        ESP_LOGI(TAG, "   Limiter Threshold: %d", LOOPBACK_LIMITER_THRESHOLD);
    }
    ESP_LOGI(TAG, "   WebSocket Stream: %s", ENABLE_WEBSOCKET_STREAM ? "启用 (需配置WiFi)" : "禁用");
    
    // 创建AFE实例
    afe_handle = esp_afe_handle_from_config(afe_config);
    if (!afe_handle) {
        ESP_LOGE(TAG, "Failed to create AFE handle!");
        return;
    }
    
    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data!");
        return;
    }
    
    afe_config_free(afe_config);
    
    // 启动双任务
    task_flag = 1;
    
    ESP_LOGI(TAG, "Starting AFE loopback tasks...");
    xTaskCreatePinnedToCore(&loopback_detect_task, "afe_play", 8 * 1024, (void*)afe_data, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(&loopback_feed_task, "afe_feed", 8 * 1024, (void*)afe_data, 5, NULL, 0);
    
    ESP_LOGI(TAG, "========== AFE Loopback Init Complete ==========\n");
}