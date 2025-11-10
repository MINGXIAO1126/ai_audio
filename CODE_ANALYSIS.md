# ESP32-S3 音频采集与WebSocket流传输系统 - 代码详细分析

## 📋 目录
1. [系统架构概述](#系统架构概述)
2. [主要模块分析](#主要模块分析)
3. [数据流程](#数据流程)
4. [关键功能详解](#关键功能详解)
5. [配置参数说明](#配置参数说明)

---

## 🏗️ 系统架构概述

### 整体架构图
```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-S3 开发板                        │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌──────────────┐      ┌──────────────┐                │
│  │   麦克风阵列   │ ────▶│   I2S 采集   │                │
│  │  (4通道)      │      │  (16kHz)     │                │
│  └──────────────┘      └──────┬───────┘                │
│                                │                          │
│                                ▼                          │
│  ┌──────────────────────────────────────┐               │
│  │      AFE 音频前端处理引擎              │               │
│  │  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐│               │
│  │  │AEC │ │ SE │ │ NS │ │VAD │ │AGC ││               │
│  │  │回声│ │语音│ │噪声│ │人声│ │增益││               │
│  │  │消除│ │增强│ │抑制│ │检测│ │控制││               │
│  │  └────┘ └────┘ └────┘ └────┘ └────┘│               │
│  └──────────┬───────────────────┬────────┘               │
│             │                   │                        │
│             ▼                   ▼                        │
│  ┌──────────────┐      ┌──────────────┐                │
│  │  本地播放      │      │  WebSocket    │                │
│  │  (扬声器)     │      │  音频流传输   │                │
│  └──────────────┘      └──────┬───────┘                │
│                                │                          │
│                                ▼                          │
│                        ┌──────────────┐                  │
│                        │  WiFi 网络    │                  │
│                        │  (STA模式)    │                  │
│                        └──────┬───────┘                  │
│                                │                          │
└────────────────────────────────┼──────────────────────────┘
                                 │
                                 ▼
                        ┌─────────────────┐
                        │  WebSocket 服务器│
                        │  (192.168.2.247) │
                        └─────────────────┘
```

### 核心功能
1. **实时音频采集**：从4通道麦克风阵列采集16kHz音频
2. **AFE音频处理**：使用ESP-SR库进行专业音频处理
3. **本地回声播放**：处理后的音频通过扬声器实时播放
4. **WebSocket流传输**：将音频数据实时发送到远程服务器

---

## 📦 主要模块分析

### 1. main.c - 主程序入口

**功能**：系统初始化和任务调度

**关键步骤**：
```c
void app_main()
{
    // 1. 初始化硬件（I2S、CODEC等）
    esp_board_init(16000, 2, 16);  // 16kHz, 2通道, 16位
    
    // 2. 初始化I/O扩展芯片（TCA9555）
    tca9555_driver_init();
    
    // 3. 开启功率放大器
    Set_EXIO(IO_EXPANDER_PIN_NUM_8, 1);
    
    // 4. 设置扬声器音量（0-100）
    esp_audio_set_play_vol(70);
    
    // 5. 初始化WiFi和WebSocket
    websocket_init();
    
    // 6. 启动AFE音频处理任务
    Audio_Loopback_AFE_Init();
}
```

**说明**：
- `esp_board_init()`: 初始化I2S接口、音频编解码器（CODEC）
- `tca9555_driver_init()`: 初始化I/O扩展芯片，用于控制外设
- `Set_EXIO()`: 通过I/O扩展器控制功率放大器使能
- `websocket_init()`: 连接WiFi并建立WebSocket连接
- `Audio_Loopback_AFE_Init()`: 启动音频处理任务

---

### 2. websocket.c - WiFi和WebSocket通信模块

#### 2.1 WiFi连接管理

**WiFi事件处理机制**：
```c
static void wifi_event_handler(...)
{
    // WiFi启动事件 → 开始连接
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    
    // WiFi断开事件 → 自动重连（最多5次）
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();  // 重连
            s_retry_num++;
        }
    }
    
    // 获取IP地址事件 → WiFi连接成功
    else if (event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
```

**关键特性**：
- ✅ 自动重连机制（最多5次）
- ✅ 使用事件组（EventGroup）同步连接状态
- ✅ 支持WPA2-PSK加密
- ✅ 等待连接完成（阻塞式）

#### 2.2 WebSocket客户端

**WebSocket事件处理**：
```c
static void websocket_event_handler(...)
{
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ws_connected = true;  // 连接成功
        break;
        
    case WEBSOCKET_EVENT_DISCONNECTED:
        ws_connected = false;  // 连接断开
        break;
        
    case WEBSOCKET_EVENT_DATA:
        // 接收服务器数据（文本或二进制）
        break;
        
    case WEBSOCKET_EVENT_ERROR:
        ws_connected = false;  // 错误处理
        break;
    }
}
```

**音频数据发送**：
```c
esp_err_t websocket_send_audio(const int16_t *audio_data, size_t length)
{
    // 发送二进制数据（opcode 0x02）
    int sent = esp_websocket_client_send_bin(ws_client, 
                                             (const char*)audio_data, 
                                             length, 
                                             portMAX_DELAY);
    return (sent < 0) ? ESP_FAIL : ESP_OK;
}
```

**说明**：
- 使用二进制帧（opcode 0x02）发送音频数据
- 缓冲区大小：8192字节（支持音频数据块）
- 自动重连超时：5秒
- 网络超时：10秒

---

### 3. mic_speech.c - AFE音频处理模块

#### 3.1 AFE（Audio Front-End）配置

**启用的算法**：
```c
afe_config->aec_init = true;      // 回声消除（Acoustic Echo Cancellation）
afe_config->se_init = true;       // 语音增强（Speech Enhancement）
afe_config->ns_init = true;       // 噪声抑制（Noise Suppression）
afe_config->vad_init = true;      // 语音活动检测（Voice Activity Detection）
afe_config->agc_init = true;      // 自动增益控制（Automatic Gain Control）
afe_config->wakenet_init = false; // 关闭唤醒词识别
```

**各算法作用**：
- **AEC（回声消除）**：消除扬声器播放声音对麦克风的反馈
- **SE（语音增强）**：使用波束成形技术增强目标方向的声音
- **NS（噪声抑制）**：降低背景噪声
- **VAD（语音活动检测）**：检测是否有人声，过滤静音和噪声
- **AGC（自动增益控制）**：自动调整音量，保持稳定输出

#### 3.2 双任务架构

**任务1：loopback_feed_task（音频采集任务）**
```c
void loopback_feed_task(void *arg)
{
    while (task_flag) {
        // 1. 从麦克风采集原始音频
        esp_get_feed_data(true, i2s_buff, ...);
        
        // 2. 送入AFE处理
        afe_handle->feed(afe_data, i2s_buff);
        
        esp_task_wdt_reset();  // 喂狗，防止看门狗超时
    }
}
```
- **运行核心**：Core 0
- **功能**：持续采集音频并送入AFE处理
- **数据流**：麦克风 → I2S → AFE引擎

**任务2：loopback_detect_task（音频播放任务）**
```c
void loopback_detect_task(void *arg)
{
    while (task_flag) {
        // 1. 从AFE获取处理后的音频
        afe_fetch_result_t* res = afe_handle->fetch(afe_data);
        
        // 2. VAD人声检测
        bool is_voice = (res->vad_state == VAD_SPEECH);
        
        // 3. 如果启用VAD过滤，只播放人声
        #if LOOPBACK_ENABLE_VAD_FILTER
        if (!is_voice) continue;  // 跳过非人声
        #endif
        
        // 4. 音频处理（放大、限幅、转立体声）
        // 5. 播放到扬声器
        esp_audio_play(stereo_buffer, ...);
        
        // 6. 发送到WebSocket（如果启用）
        #if ENABLE_WEBSOCKET_STREAM
        websocket_send_audio(res->data, res->data_size);
        #endif
    }
}
```
- **运行核心**：Core 1
- **功能**：获取AFE处理后的音频，播放并传输
- **数据流**：AFE → 处理 → 扬声器 + WebSocket

#### 3.3 音频处理算法

**软限幅器（Soft Limiter）**：
```c
static inline int16_t soft_limiter(int32_t sample)
{
    if (sample > LOOPBACK_LIMITER_THRESHOLD) {
        // 平滑压缩超过阈值的部分（压缩比 3:1）
        int32_t excess = sample - LOOPBACK_LIMITER_THRESHOLD;
        sample = LOOPBACK_LIMITER_THRESHOLD + (excess / 3);
    }
    // 硬限幅到16位范围
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (int16_t)sample;
}
```

**作用**：
- 防止音频削波失真（避免刺耳声音）
- 超过阈值时平滑压缩，而不是硬截断
- 保持音频动态范围

**单声道转立体声**：
```c
for (int i = 0; i < output_samples; i++) {
    int16_t sample = res->data[i];  // 单声道输入
    int32_t amplified = sample * LOOPBACK_OUTPUT_AMPLIFY;  // 放大
    int16_t limited = soft_limiter(amplified);  // 限幅
    
    stereo_buffer[i * 2 + 0] = limited;  // 左声道
    stereo_buffer[i * 2 + 1] = limited;  // 右声道（复制）
}
```

---

## 🔄 数据流程

### 完整数据流图
```
┌─────────────────────────────────────────────────────────────┐
│  1. 音频采集阶段                                              │
├─────────────────────────────────────────────────────────────┤
│  麦克风阵列 (4通道)                                           │
│      │                                                       │
│      ▼                                                       │
│  I2S接口 (16kHz, 16bit)                                      │
│      │                                                       │
│      ▼                                                       │
│  loopback_feed_task (Core 0)                                │
│      │                                                       │
│      ▼                                                       │
│  AFE引擎处理                                                 │
│  ┌─────┬─────┬─────┬─────┬─────┐                          │
│  │ AEC │ SE  │ NS  │ VAD │ AGC │                          │
│  └─────┴─────┴─────┴─────┴─────┘                          │
│      │                                                       │
└──────┼───────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│  2. 音频输出阶段                                              │
├─────────────────────────────────────────────────────────────┤
│  loopback_detect_task (Core 1)                              │
│      │                                                       │
│      ├─────────────────┬─────────────────┐                  │
│      │                 │                 │                  │
│      ▼                 ▼                 ▼                  │
│  ┌─────────┐    ┌──────────────┐  ┌──────────────┐        │
│  │ VAD过滤 │    │ 音量放大     │  │ 软限幅器     │        │
│  │ (可选)  │    │ (×3倍)       │  │ (防削波)     │        │
│  └────┬────┘    └──────┬───────┘  └──────┬───────┘        │
│       │                │                 │                  │
│       └────────────────┼─────────────────┘                  │
│                        │                                    │
│                        ▼                                    │
│               ┌──────────────────┐                          │
│               │ 单声道→立体声转换 │                          │
│               └────────┬─────────┘                          │
│                        │                                    │
│        ┌───────────────┼───────────────┐                  │
│        │               │               │                    │
│        ▼               ▼               ▼                    │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ 扬声器   │  │  WebSocket   │  │  统计信息    │         │
│  │ (本地)   │  │  (远程传输)  │  │  (日志)     │         │
│  └──────────┘  └──────────────┘  └──────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### 数据格式说明

**输入格式**：
- 采样率：16kHz
- 位深：16位
- 通道：4通道（麦克风阵列）→ 处理为单声道

**AFE输出格式**：
- 采样率：16kHz
- 位深：16位
- 通道：单声道（经过波束成形）

**播放格式**：
- 采样率：16kHz
- 位深：16位
- 通道：立体声（单声道复制到左右声道）

**WebSocket传输格式**：
- 格式：二进制（opcode 0x02）
- 数据：AFE处理后的单声道16位PCM
- 大小：每个数据块约512-1024字节

---

## ⚙️ 关键功能详解

### 1. VAD人声过滤

**原理**：
```c
bool is_voice_detected = (res->vad_state == VAD_SPEECH);

#if LOOPBACK_ENABLE_VAD_FILTER
if (!is_voice_detected) {
    continue;  // 跳过非人声音频，不播放
}
#endif
```

**效果**：
- ✅ 只播放检测到人声时的音频
- ✅ 自动过滤背景噪声、静音
- ✅ 降低不必要的音频播放和传输

**VAD模式**：
- `VAD_MODE_0` ~ `VAD_MODE_4`（数字越大越灵敏）
- 当前配置：`VAD_MODE_3`（较高灵敏度）

### 2. 自动增益控制（AGC）

**参数配置**：
```c
afe_config->agc_compression_gain_db = 15;      // 压缩增益 15dB
afe_config->agc_target_level_dbfs = 6;        // 目标电平 -6dBFS
afe_config->afe_linear_gain = 10.0f;          // 线性增益 10倍
```

**作用**：
- **压缩增益**：增强小音量信号（远距离收音）
- **目标电平**：控制输出音量水平
- **线性增益**：直接放大输入信号（提高麦克风灵敏度）

### 3. 回声消除（AEC）

**配置**：
```c
afe_config->aec_filter_length = 4;  // 滤波器长度
```

**作用**：
- 消除扬声器播放声音对麦克风的反馈
- 防止啸叫（feedback）
- 滤波器长度越大，效果越好，但CPU占用越高

### 4. WebSocket音频流传输

**传输时机**：
```c
#if ENABLE_WEBSOCKET_STREAM
if (websocket_is_connected()) {
    // 发送AFE处理后的原始音频数据
    websocket_send_audio(res->data, res->data_size);
}
#endif
```

**数据特点**：
- 实时传输（每个AFE数据块立即发送）
- 二进制格式（高效）
- 包含VAD过滤后的音频（如果启用）

---

## 🔧 配置参数说明

### WiFi配置（websocket.h）
```c
#define WIFI_SSID           "Chenhh"              // WiFi名称
#define WIFI_PASSWORD       "Chenhh520"            // WiFi密码
#define WIFI_MAXIMUM_RETRY  5                     // 最大重连次数
```

### WebSocket配置（websocket.h）
```c
#define WEBSOCKET_URI       "ws://192.168.2.247:6006/ws"  // 服务器地址
```

### 音频处理配置（mic_speech.c）

**AFE参数**：
```c
#define LOOPBACK_LINEAR_GAIN        10.0f   // 线性增益（提高灵敏度）
#define LOOPBACK_AGC_COMPRESSION_GAIN  15   // AGC压缩增益（dB）
#define LOOPBACK_AGC_TARGET_LEVEL      6    // AGC目标电平（-dBFS）
#define LOOPBACK_AEC_FILTER_LENGTH     4    // AEC滤波器长度
```

**音频输出参数**：
```c
#define LOOPBACK_OUTPUT_AMPLIFY        3    // 输出放大倍数
#define LOOPBACK_LIMITER_THRESHOLD  28000   // 软限幅器阈值
```

**功能开关**：
```c
#define LOOPBACK_ENABLE_VAD_FILTER  true    // 启用VAD人声过滤
#define LOOPBACK_ENABLE_SOFT_LIMITER true   // 启用软限幅器
#define ENABLE_WEBSOCKET_STREAM     true    // 启用WebSocket传输
```

### 扬声器音量（main.c）
```c
#define SPEAKER_VOLUME  70  // 扬声器音量（0-100）
```

---

## 📊 性能特性

### 实时性
- **音频延迟**：< 100ms（AFE处理 + 播放）
- **WebSocket延迟**：取决于网络延迟（通常 < 50ms）

### 资源占用
- **CPU占用**：Core 0 ~30%，Core 1 ~40%
- **内存占用**：约200KB（SPIRAM）
- **网络带宽**：约256 kbps（16kHz × 16bit × 1ch）

### 音频质量
- **采样率**：16kHz（语音质量）
- **位深**：16位
- **动态范围**：96dB（理论值）

---

## 🔍 调试与监控

### 日志输出
系统会定期输出状态信息：
```
[PLAY] Count:100, VAD:语音85/静音15, Vol:-12.5 dB, Max:12345, OK:99, Fail:1
```

**含义**：
- `Count`: 处理的数据块计数
- `VAD:语音X/静音Y`: VAD检测统计
- `Vol`: 音频音量（dB）
- `Max`: 最大采样值
- `OK/Fail`: 播放成功/失败次数

### 常见问题排查

1. **无声音输出**
   - 检查功率放大器是否使能
   - 检查扬声器音量设置
   - 检查VAD过滤是否过于严格

2. **WebSocket连接失败**
   - 检查WiFi连接状态
   - 检查服务器地址和端口
   - 检查防火墙设置

3. **音频质量差**
   - 调整AGC参数
   - 调整线性增益
   - 检查麦克风位置

---

## 📝 总结

这是一个完整的**实时音频采集、处理和传输系统**，具有以下特点：

✅ **专业音频处理**：使用ESP-SR AFE引擎，支持AEC、SE、NS、VAD、AGC  
✅ **智能人声过滤**：只播放和传输人声，过滤噪声  
✅ **实时网络传输**：通过WebSocket实时传输音频数据  
✅ **高质量输出**：软限幅器防止失真，AGC自动增益控制  
✅ **可配置性强**：所有关键参数都可以调整  

适用于**语音对讲、远程监控、智能音箱**等应用场景。

