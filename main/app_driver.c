#include "app_driver.h"
#include "websocket_client.h"
#include "Mic_driver.h"

#define TAG "app_driver"

// 开始任务
#define START_TASK_DEPTH 1024 // 任务栈深
#define START_TASK_PRI   4 // 任务优先级

// 语音播放任务
#define AUDIO_TASK_DEPTH 4096 // 任务栈深
#define AUDIO_TASK_PRI   3 // 任务优先级

// websocket发送任务
#define WS_SEND_TASK_DEPTH 2048 // 任务栈深
#define WS_SEND_TASK_PRI   2 // 任务优先级

//语音播放任务
static void audio_loop_task(void *param)
{
    ESP_LOGI(TAG,"语音播放任务开始");
    while (1) {
        esp_err_t ret = mic_read();
        if (ret == ESP_OK) 
        {
            spk_write();
        }else
        {
            ESP_LOGW(TAG,"Mic 读取失败了：%s",esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

//websocket发送任务
static void ws_send_task(void *param) {
    wsSendData_t recv_data;

    while (1) {
        // 从队列接收待发送的音频数据（阻塞等待，不影响音频任务）
        if (xQueueReceive(ws_send_queue, //队列句柄
                        &recv_data, //接收数据
                        portMAX_DELAY)) //阻塞等待（不占用CPU）
        {
            // 仅当WS连接时发送，超时设为100ms（非无限阻塞）
            if (ws_client && esp_websocket_client_is_connected(ws_client)) 
            {
                esp_err_t ret = esp_websocket_client_send_bin(ws_client, 
                                                            (const char *)recv_data.audio_buf,
                                                             recv_data.data_len, 
                                                             pdMS_TO_TICKS(100));
                if (ret != ESP_OK) {
                    ESP_LOGW("WS", "Send failed: %s", esp_err_to_name(ret));
                } else {
                    ESP_LOGI("WS", "Sent %d bytes to server", recv_data.data_len);
                }
            }
        }
    }
}

//开始任务函数入口
void start_task(void *param)
{
    //语音播放任务
    xTaskCreate(audio_loop_task,"audio loop task",AUDIO_TASK_DEPTH,NULL,AUDIO_TASK_PRI,NULL);

    //websocket发送任务
    xTaskCreate(ws_send_task,"ws send task",WS_SEND_TASK_DEPTH,NULL,WS_SEND_TASK_PRI,NULL);

    //删除启动任务
    vTaskDelete(NULL);
}

void test_task(void )
{
    xTaskCreate(start_task,"start task",START_TASK_DEPTH,NULL,START_TASK_PRI,NULL);
}