#ifndef __SPE_H_
#define __SPE_H_

//MAX98357A引脚
#define MAX_DIN     GPIO_NUM_15
#define MAX_BCLK    GPIO_NUM_7
#define MAX_LRC     GPIO_NUM_16

extern i2s_chan_handle_t tx_handle;

esp_err_t i2s_tx_init(void);
esp_err_t spk_write(void);

#endif