#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Audio_common.h"
#include "app_driver.h"
#include "wifi_connect.h"
#include "websocket_client.h"

void app_main(void){
    i2s_tx_init();//MAX98357A初始化
 
    i2s_rx_init();//INMP441初始化

    wifi_connect();//wifi连接初始化

    // 初始化WebSocket发送队列（新增）
    ws_send_queue = xQueueCreate(WS_SEND_QUEUE_LEN,     // 队列长度（4个元素）
                            sizeof(wsSendData_t));     // 每个元素的大小（BUF_SIZE + 4字节）
    if (ws_send_queue == NULL) 
    {
        ESP_LOGE("QUEUE", "Create WS send queue failed");
        return;
    }

    websocket_client_app_start();//websocket客户端初始化

    test_task();
}
