# ===============================================
# 麦克风回声 + WebSocket 音频流传输
# ===============================================

# 源文件列表
set(SOURCES
    "./main.c"
    "./hardeware_driver/bsp_board.c"
    "./tca9555_driver/tca9555_driver.c"
    "./speech_det_driver/mic_speech.c"
    "./websocket_driver/websocket.c"
)

# 头文件目录
set(INCLUDE_DIRS 
    "./hardeware_driver"
    "./tca9555_driver"
    "./speech_det_driver"
    "./websocket_driver"
    "."
)

# 注册组件（添加网络和驱动依赖）
# 注意：esp_websocket_client 通过 idf_component.yml 管理
idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES 
        # 网络相关
        nvs_flash 
        esp_wifi 
        esp_netif 
        esp_event
        # 驱动相关 (ESP-IDF 5.4+ 驱动组件化)
        esp_driver_gpio
        esp_driver_i2s
        esp_driver_i2c
        esp_driver_sdmmc
    PRIV_REQUIRES
        # 私有依赖
        fatfs
)

