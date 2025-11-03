#初始化源文件
set(SOURCES
    "./audio/Mic_driver.c"
    "./audio/Speaker_driver.c"
    "./wifi/wifi_connect.c"
    "./websocket/websocket_client.c"
)

set(INCLUDE_DIRS
    "./audio"
    "./wifi"
    "./websocket"
)


#注册组件
idf_component_register(SRCS "main.c" "app_driver.c" ${SOURCES}
                    PRIV_REQUIRES   esp_driver_gpio 
                                    i2s_examples_common 
                                    driver 
                                    esp_wifi 
                                    nvs_flash
                    INCLUDE_DIRS "." ${INCLUDE_DIRS})
