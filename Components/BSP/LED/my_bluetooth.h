#ifndef __MY_BLUETOOTH_H__
#define __MY_BLUETOOTH_H__

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_bt_device.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_spp_api.h"
#include "esp_gap_bt_api.h"
#include "dht11.h"

// 蓝牙的配置
#define BT_TARGET_ADDR {0xb0, 0xcb, 0xd8, 0xe1, 0xf6, 0xbe}
#define BT_DEVICE_NAME "ESP32_TEMP_MONITOR"
#define BT_RECONNECT_INTERVAL 5000

// DHT11 全局变量
extern DHT11_Message dht11_data;

// 定时器句柄（外部可见）
extern TimerHandle_t reconnect_timer;

// 公共函数声明（去掉 static）
esp_err_t bluetooth_init(void);
esp_err_t bluetooth_send_char(char c);
void temp_monitor_task(void *pvParameters);
void reconnect_timer_callback(TimerHandle_t xTimer);

#endif // __MY_BLUETOOTH_H__