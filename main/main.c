#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "bsp_board.h"
#include "tca9555_driver.h"
#include "mic_speech.h"  // 使用AFE音频处理算法
#include "websocket.h"   // WiFi 和 WebSocket 音频流传输



static const char *TAG = "MIC_LOOPBACK";

// ===============================================
// 扬声器音量配置 (0-100)
// ===============================================
#define SPEAKER_VOLUME  70  // 扬声器音量百分比 (范围: 0~100)
                            // 默认: 80
                            // 增大此值可提高扬声器音量
                            // 建议值: 70~95 (过高可能失真)


void app_main()
{
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  麦克风实时回声测试程序 (AFE版本)");
    ESP_LOGI(TAG, "  Microphone Loopback with AFE");
    ESP_LOGI(TAG, "========================================\n");
    
    // 1. 初始化硬件（I2S、CODEC等）
    ESP_LOGI(TAG, "1. Initializing board hardware...");
    ESP_ERROR_CHECK(esp_board_init(16000, 2, 16));
    ESP_LOGI(TAG, "   Board initialized (16kHz, 2ch, 16bit)\n");
    
    // 2. 初始化I/O扩展芯片
    ESP_LOGI(TAG, "2. Initializing TCA9555 I/O expander...");
    tca9555_driver_init();
    ESP_LOGI(TAG, "  I/O expander initialized\n");
    
    // 3. 开启功率放大器
    ESP_LOGI(TAG, "3. Enabling power amplifier...");
    Set_EXIO(IO_EXPANDER_PIN_NUM_8, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "  Power amplifier enabled\n");
    
    // 4. 设置扬声器音量
    ESP_LOGI(TAG, "4. Setting speaker volume to %d%%...", SPEAKER_VOLUME);
    esp_audio_set_play_vol(SPEAKER_VOLUME);
    ESP_LOGI(TAG, "  Speaker volume set\n");
    
    
    ESP_LOGI(TAG, "5. Initializing WiFi and WebSocket...");
    esp_err_t ws_ret = websocket_init();
    if (ws_ret == ESP_OK) {
        ESP_LOGI(TAG, "   ✓ WebSocket initialized\n");
    } else {
        ESP_LOGW(TAG, "   ⚠ WebSocket init failed, continuing without streaming\n");
    }
    
    
    // 6. 启动AFE音频回声任务（包含AEC、SE、NS、VAD、AGC）
    ESP_LOGI(TAG, "5. Starting AFE audio loopback...");
    Audio_Loopback_AFE_Init();
    ESP_LOGI(TAG, "   ✓ AFE loopback started\n");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  系统初始化完成！");
    ESP_LOGI(TAG, "  对着麦克风说话，应该能听到高质量回声");
    ESP_LOGI(TAG, "  (经过AEC/SE/NS/VAD/AGC算法处理)");
    ESP_LOGI(TAG, "========================================\n");
    
    // 主循环 - 保持任务运行
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
}

