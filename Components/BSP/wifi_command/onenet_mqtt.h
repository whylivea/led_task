#ifndef __onenet_mqtt_h_
#define __onenet_mqtt_h_
#include "esp_err.h"
#include "cJSON.h"
#define ONENET_PRODUCT_ID  "qsR22SwPeb"
#define ONENET_PRODUCT_ACCESS_KEY  "Mx0/lfvzkylj7WNZ12/x09nPuWE/TU+HAYEdT11PfTs="
#define ONENET_device_NAME "my_DHT11"

esp_err_t onenet_start(void);
static void onenet_property_ack(const char* id, int code, const char* msg);
void onenet_subscribe(void);
esp_err_t onenet_post_property_data(const char*data);

//任务上报函数
void onenet_start_report_task(void);
#endif