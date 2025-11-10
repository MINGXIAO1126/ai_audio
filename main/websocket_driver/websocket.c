#include "websocket.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WebSocket";

// ===============================================
// WiFi 事件标志
// ===============================================
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool wifi_connected = false;
static bool ws_connected = false;

// ===============================================
// WebSocket 客户端句柄
// ===============================================
static esp_websocket_client_handle_t ws_client = NULL;

// ===============================================
// WiFi 事件处理
// ===============================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi STA started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to WiFi... (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to WiFi after %d retries", WIFI_MAXIMUM_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ===============================================
// WebSocket 事件处理
// ===============================================
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected to server");
        ws_connected = true;
        break;
        
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected from server");
        ws_connected = false;
        break;
        
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WebSocket received data, opcode=%d, len=%d", 
                 data->op_code, data->data_len);
        if (data->op_code == 0x01) {  // Text frame
            ESP_LOGI(TAG, "Received text: %.*s", data->data_len, (char *)data->data_ptr);
        }
        break;
        
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error occurred");
        ws_connected = false;
        break;
        
    default:
        break;
    }
}

// ===============================================
// WiFi 初始化
// ===============================================
static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化 TCP/IP 协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 创建默认 WiFi STA
    esp_netif_create_default_wifi_sta();
    
    // WiFi 初始化配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 注册事件处理
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_got_ip));
    
    // WiFi 配置
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization finished.");
    ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);
    
    // 等待连接结果
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✓ Connected to WiFi SSID: %s", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "✗ Failed to connect to WiFi SSID: %s", WIFI_SSID);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "✗ Unexpected WiFi connection event");
        return ESP_ERR_TIMEOUT;
    }
}

// ===============================================
// WebSocket 初始化
// ===============================================
static esp_err_t websocket_client_init(void)
{
    if (!wifi_connected) {
        ESP_LOGE(TAG, "WiFi not connected, cannot initialize WebSocket");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing WebSocket client...");
    ESP_LOGI(TAG, "WebSocket URI: %s", WEBSOCKET_URI);
    
    esp_websocket_client_config_t ws_cfg = {
        .uri = WEBSOCKET_URI,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .buffer_size = 8192,  // 增大缓冲区以支持音频数据
    };
    
    ws_client = esp_websocket_client_init(&ws_cfg);
    if (ws_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return ESP_FAIL;
    }
    
    // 注册事件处理
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, 
                                   websocket_event_handler, NULL);
    
    // 启动 WebSocket 客户端
    esp_err_t ret = esp_websocket_client_start(ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WebSocket client started successfully");
    
    // 等待连接建立（最多等待5秒）
    int wait_count = 0;
    while (!ws_connected && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    if (ws_connected) {
        ESP_LOGI(TAG, "✓ WebSocket connected successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "⚠ WebSocket connection still in progress...");
        return ESP_OK;  // 返回成功，让连接在后台继续
    }
}

// ===============================================
// 公共接口实现
// ===============================================

esp_err_t websocket_init(void)
{
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "  WebSocket Audio Streaming Init");
    ESP_LOGI(TAG, "========================================");
    
    // 1. 初始化 WiFi
    ESP_LOGI(TAG, "1. Initializing WiFi...");
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return ret;
    }
    
    // 2. 初始化 WebSocket
    ESP_LOGI(TAG, "\n2. Initializing WebSocket...");
    ret = websocket_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket");
        return ret;
    }
    
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "  WebSocket initialization complete!");
    ESP_LOGI(TAG, "========================================\n");
    
    return ESP_OK;
}

esp_err_t websocket_deinit(void)
{
    ws_connected = false;
    wifi_connected = false;
    
    if (ws_client) {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        ws_client = NULL;
        ESP_LOGI(TAG, "WebSocket client destroyed");
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "WiFi deinitialized");
    
    return ESP_OK;
}

bool websocket_is_wifi_connected(void)
{
    return wifi_connected;
}

bool websocket_is_connected(void)
{
    return ws_connected;
}

esp_err_t websocket_send_audio(const int16_t *audio_data, size_t length)
{
    if (!ws_connected || ws_client == NULL) {
        // ESP_LOGW(TAG, "WebSocket not connected, skipping audio data");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 服务器期望接收 float32 格式的音频数据（范围 -1.0 到 1.0）
    //多数音频处理库默认使用float32，且约定音频样本的值在-1.0到1.0之间
    // 需要将 int16_t 转换为 float32 并归一化
    size_t sample_count = length / sizeof(int16_t);
    float *float_buffer = heap_caps_malloc(sample_count * sizeof(float), MALLOC_CAP_SPIRAM);
    
    if (float_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate float buffer for audio conversion");
        return ESP_ERR_NO_MEM;
    }
    
    // 转换 int16_t 到 float32，并归一化到 [-1.0, 1.0]
    // 公式：float_value = int16_value / 32768.0f
    for (size_t i = 0; i < sample_count; i++) {
        float_buffer[i] = (float)audio_data[i] / 32768.0f;
    }
    
    // 发送二进制数据（opcode 0x02）- float32 格式
    size_t float_length = sample_count * sizeof(float);
    int sent = esp_websocket_client_send_bin(ws_client, (const char*)float_buffer, 
                                             float_length, portMAX_DELAY);
    
    free(float_buffer);
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send audio data via WebSocket");
        return ESP_FAIL;
    }
    
    // ESP_LOGD(TAG, "Sent %d bytes of float32 audio data (%d samples)", sent, sample_count);
    return ESP_OK;
}

esp_err_t websocket_reconnect(void)
{
    ESP_LOGI(TAG, "Attempting to reconnect WebSocket...");
    
    if (ws_client) {
        esp_websocket_client_stop(ws_client);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        esp_err_t ret = esp_websocket_client_start(ws_client);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "WebSocket reconnect initiated");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "WebSocket reconnect failed");
            return ret;
        }
    }
    
    return ESP_ERR_INVALID_STATE;
}

