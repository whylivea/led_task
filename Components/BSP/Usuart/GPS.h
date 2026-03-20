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

// 经纬度结构体
typedef struct {
    double latitude;
    double longitude;
    char ns_indicator;  // 'N' or 'S'
    char ew_indicator;  // 'E' or 'W'
    int fix_quality;    // 定位质量 0=无效,1=GPS,2=DGPS
    int satellites;      // 卫星数量
    float altitude;      // 海拔高度
    int hour, minute, second;  // UTC时间
    int day, month, year;      // UTC日期
    bool valid;          // 数据是否有效
} gps_data_t;

extern gps_data_t gps_data;
void start_gps_task(void);



#endif