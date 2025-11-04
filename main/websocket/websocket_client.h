#ifndef __WEB_CLI_H_
#define __WEB_CLI_H_

#include <string.h>
#include "esp_log.h"
#include "esp_websocket_client.h"

extern esp_websocket_client_handle_t ws_client;//websocket连接句柄

void websocket_client_app_start(void);

#endif