#ifndef __dht11_h_
#define __dht11_h_

#include "esp_log.h"
#include "driver\gpio.h"
#include "freertos\FreeRTOS.h"
#include "freertos\task.h"
#include "oled.h"
#include "OLED_Data.h"
typedef struct 
{
    uint8_t temperature_int;//温度整数
    uint8_t temperature_dec;//温度小数
    uint8_t humidity_int;//湿度整数
    uint8_t humidity_dec;//湿度小数
    uint8_t checksum;//校验和
    bool valid;//数据是不是有效的
}DHT11_Message;

extern DHT11_Message dht11_data;

esp_err_t DHT11_Init(gpio_num_t dht11_gpio);
 esp_err_t dht11_read_data(gpio_num_t DHT11_GPIO);
#endif