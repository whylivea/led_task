#ifndef __GPS_H__
#define __GPS_H__

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "Usuart.h"
#define TAG "GPS"

// GPS NMEA 数据缓冲区
#define GPS_RX_BUF_SIZE 1024
#define GPS_MAX_SENTENCE_LEN 256

void start_gps_task(void);



#endif