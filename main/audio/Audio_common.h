#ifndef __AUDIO_H_
#define __AUDIO_H_

#include <stdio.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"


//配置rx对INMP441的采样率为44.1kHz，这是常用的人声采样率
#define SAMPLE_RATE 44100

//buf size计算方法：根据esp32官方文档，buf size = dma frame num * 声道数 * 数据位宽 / 8
#define BUF_SIZE (1023 * 1 * 32 / 8) //4092
 
//音频buffer
extern uint8_t buf[BUF_SIZE];

#endif