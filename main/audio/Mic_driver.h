#ifndef __MIC_H_
#define __MIC_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

//INMP引脚
#define INMP_SD     GPIO_NUM_5
#define INMP_SCK    GPIO_NUM_4
#define INMP_WS     GPIO_NUM_6

typedef struct {
    float gain;           // 增益倍数
    float compression_threshold; // 压缩阈值
    float compression_ratio;     // 压缩比例
    bool enable_agc;      // 是否启用自动增益
} audio_processor_t;


extern i2s_chan_handle_t rx_handle;

esp_err_t i2s_rx_init(void);
esp_err_t mic_read(void);
void amplify_audio_buffer(void* buffer, size_t bytes, float gain);
void compress_audio_buffer(void* buffer, size_t bytes, float threshold, float ratio);
void process_audio_buffer(void* buffer, size_t bytes, audio_processor_t* proc);

#endif