#include "Audio_common.h"
#include "Speaker_driver.h"
#include "esp_log.h"

#define TAG "SPEAKER"

i2s_chan_handle_t tx_handle = NULL;

//初始化tx，用于向MAX98357A写数据
esp_err_t i2s_tx_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = 511;
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);
 
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .din = I2S_GPIO_UNUSED,
            .bclk = MAX_BCLK,
            .ws = MAX_LRC,
            .dout = MAX_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
 
    i2s_channel_init_std_mode(tx_handle, &std_cfg);
 
    i2s_channel_enable(tx_handle);

    return ESP_OK;
}

//音频播放
esp_err_t spk_write(void)
{
    size_t bytes = 0;
    esp_err_t ret = i2s_channel_write(tx_handle, buf,BUF_SIZE,&bytes,1000);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG,"SPEAKER 写入失败：%s",esp_err_to_name(ret));
    }else if (bytes == 0)
    {
        ESP_LOGW(TAG,"写入0字节");
    }

    return ret;
}