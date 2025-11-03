#include "websocket_client.h"

#define TAG  "websocket_client"

#define SERVICE_URI   "ws://192.168.2.247:6006/ws"

esp_websocket_client_handle_t ws_client = NULL;//websocket连接句柄

//事件回调函数
static void websocket_event_handler(void *handler_args, //注册回调时传入的参数
                                    esp_event_base_t base, //事件的类别，websocket固定是WEBSOCKET_EVENT
                                    int32_t event_id,//具体的事件类型，如断开、连接、接收数据
                                    void *event_data)//指向事件数据的结构体指针
{
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI("WS", "WebSocket connected");//客户端成功连接到服务器
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW("WS", "WebSocket disconnected");//连接断开
            break;
        case WEBSOCKET_EVENT_DATA://收到数据
            ESP_LOGI("WS", "Received data from server");
            break;
        default:
            break;
    }
}

//websocket客户端初始化
void websocket_client_app_start(void)
{
    //websocket客户端配置
    esp_websocket_client_config_t websocket_cfg = {
        .uri = SERVICE_URI,
        .task_prio = 5,//任务优先级
        .task_stack = 4096,//任务的堆栈大小
        .buffer_size = 1024,//发送、接收缓冲区大小
        .disable_auto_reconnect = true,//自动重连
        .ping_interval_sec = 10//心跳间隔，客户端每隔10秒自动发送一个ping帧，服务器回复，用于检测连接是否还活着
    };

    //创建websocket客户端实例
    ws_client = esp_websocket_client_init(&websocket_cfg);

    //注册事件回调函数
    esp_websocket_register_events(ws_client,WEBSOCKET_EVENT_ANY,websocket_event_handler,NULL);

    //启动客户端并连接服务器
    esp_websocket_client_start(ws_client);  
}



