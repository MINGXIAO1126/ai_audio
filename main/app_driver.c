#include "app_driver.h"
#include "websocket_client.h"
#include "Mic_driver.h"

#define TAG "app_driver"

// 开始任务
#define START_TASK_DEPTH 1024 // 任务栈深
#define START_TASK_PRI   4 // 任务优先级

// 语音播放任务
#define AUDIO_TASK_DEPTH 8192 // 任务栈深
#define AUDIO_TASK_PRI   3 // 任务优先级


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

//开始任务函数入口
void start_task(void *param)
{
    //语音播放任务
    xTaskCreate(audio_loop_task,"audio loop task",AUDIO_TASK_DEPTH,NULL,AUDIO_TASK_PRI,NULL);

    //删除启动任务
    vTaskDelete(NULL);
}

void test_task(void )
{
    xTaskCreate(start_task,"start task",START_TASK_DEPTH,NULL,START_TASK_PRI,NULL);
}