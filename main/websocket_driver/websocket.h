#ifndef __WEBSOCKET_H__
#define __WEBSOCKET_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// ===============================================
// WiFi 配置参数 - 请修改为你的 WiFi 信息
// ===============================================
#define WIFI_SSID           "Chenhh"          // WiFi名称
#define WIFI_PASSWORD       "Chenhh520"      // WiFi密码
#define WIFI_MAXIMUM_RETRY  5                       // WiFi最大重连次数

// ===============================================
// WebSocket 配置参数 - 请修改为你的服务器地址
// ===============================================
#define WEBSOCKET_URI       "ws://192.168.2.247:6006/ws"  // WebSocket服务器地址

// ===============================================
// 音频数据配置
// ===============================================
#define AUDIO_SAMPLE_RATE   16000    // 采样率 16kHz
#define AUDIO_CHANNELS      1        // 单声道
#define AUDIO_BITS          16       // 16位采样
#define AUDIO_BUFFER_SIZE   4096     // 音频缓冲区大小（字节）

// ===============================================
// 函数接口
// ===============================================

/**
 * @brief 初始化 WiFi 和 WebSocket
 * @return ESP_OK 成功, 其他值失败
 */
esp_err_t websocket_init(void);

/**
 * @brief 反初始化 WiFi 和 WebSocket
 * @return ESP_OK 成功, 其他值失败
 */
esp_err_t websocket_deinit(void);

/**
 * @brief 检查 WiFi 是否已连接
 * @return true 已连接, false 未连接
 */
bool websocket_is_wifi_connected(void);

/**
 * @brief 检查 WebSocket 是否已连接
 * @return true 已连接, false 未连接
 */
bool websocket_is_connected(void);

/**
 * @brief 发送音频数据到 WebSocket 服务器（二进制格式）
 * @param audio_data 音频数据指针
 * @param length 音频数据长度（字节）
 * @return ESP_OK 成功, 其他值失败
 */
esp_err_t websocket_send_audio(const int16_t *audio_data, size_t length);

/**
 * @brief 重新连接 WebSocket
 * @return ESP_OK 成功, 其他值失败
 */
esp_err_t websocket_reconnect(void);

#endif // __WEBSOCKET_H__

