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

    websocket_client_app_start();//websocket客户端初始化

    test_task();
}
