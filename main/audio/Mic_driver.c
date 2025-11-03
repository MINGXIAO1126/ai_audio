#include "Audio_common.h"
#include "esp_log.h"
#include "Mic_driver.h"
#include <math.h>
#include "websocket_client.h"

#define TAG  "INMP441"


uint8_t buf[BUF_SIZE] = {0};
i2s_chan_handle_t rx_handle =NULL;
QueueHandle_t ws_send_queue = NULL;//websocket发送队列

audio_processor_t audio_proc = {
    .gain = 15.0f,//增益倍数
    .compression_threshold = 10000000.0f,//压缩阈值
    .compression_ratio = 1.0f,//压缩比例
    .enable_agc = true//是否启用自动增益
};

//初始化i2s rx，用于从INMP441接收数据
esp_err_t i2s_rx_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    
    //dma frame num使用最大值，增大dma一次搬运的数据量，能够提高效率，减小杂音，使用1023可以做到没有一丝杂音
    chan_cfg.dma_frame_num = 511;
    i2s_new_channel(&chan_cfg, NULL, &rx_handle);
 
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        
        //虽然inmp441采集数据为24bit，但是仍可使用32bit来接收，中间存储过程不需考虑，只要让声音怎么进来就怎么出去即可
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .bclk = INMP_SCK,
            .ws = INMP_WS,
            .din = INMP_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
 
    i2s_channel_init_std_mode(rx_handle, &std_cfg);
 
    i2s_channel_enable(rx_handle);

    return ESP_OK;
}

//软件放大
void amplify_audio_buffer(void* buffer, size_t bytes, float gain)
{
    // 假设是32位数据（24位数据存储在32位容器中）
    int32_t* samples = (int32_t*)buffer;
    size_t sample_count = bytes / sizeof(int32_t);
    
    for (size_t i = 0; i < sample_count; i++) {
        // 应用增益，注意防止溢出
        int64_t amplified = (int64_t)samples[i] * gain;
        
        // 限制在32位范围内
        if (amplified > INT32_MAX) {
            samples[i] = INT32_MAX;
        } else if (amplified < INT32_MIN) {
            samples[i] = INT32_MIN;
        } else {
            samples[i] = (int32_t)amplified;
        }
    }
}

//动态范围压缩
void compress_audio_buffer(void* buffer, size_t bytes, float threshold, float ratio)
{
    int32_t* samples = (int32_t*)buffer;
    size_t sample_count = bytes / sizeof(int32_t);
    
    for (size_t i = 0; i < sample_count; i++) {
        float sample = (float)samples[i];
        float abs_sample = fabsf(sample);
        
        if (abs_sample > threshold) {
            // 超过阈值的部分按比例压缩
            float excess = abs_sample - threshold;
            float compressed_excess = excess / ratio;
            float compressed_sample = threshold + compressed_excess;
            
            samples[i] = (int32_t)(copysignf(compressed_sample, sample));
        }
    }
}

//音频处理
void process_audio_buffer(void* buffer, size_t bytes, audio_processor_t* proc)
{
    if (proc->enable_agc) {
        compress_audio_buffer(buffer, bytes, proc->compression_threshold, proc->compression_ratio);
    }
    
    amplify_audio_buffer(buffer, bytes, proc->gain);
}

//音频读取
esp_err_t mic_read(void)
{
    size_t bytes = 0;
    esp_err_t ret = i2s_channel_read(rx_handle,buf,BUF_SIZE,&bytes,1000);

    if (ret == ESP_OK && bytes > 0)
    {
        process_audio_buffer(buf,bytes,&audio_proc);
    }

    //将数据放入队列，然后新建一个任务用于websocket数据发送
    if(ws_send_queue != NULL)
    {
        // 入队
        if (ws_send_queue != NULL) 
        {
            wsSendData_t send_data;
            
            // 复制音频数据到结构体的数组中
            memcpy(send_data.audio_buf, buf, bytes);

            // 赋值有效数据长度（
            send_data.data_len = bytes;

            // 非阻塞入队（队列满则丢弃，不阻塞音频任务）
            if (xQueueSend(ws_send_queue, &send_data, 0) != pdPASS) 
            {
                ESP_LOGW("QUEUE", "WS send queue full, drop data");
            }
        }
    } else {
        ESP_LOGW("MIC", "Read failed: ret=%s, bytes=%d", esp_err_to_name(ret), bytes);
    }

    return ret;
}
