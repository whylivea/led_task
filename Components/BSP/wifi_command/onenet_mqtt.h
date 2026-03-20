#ifndef __onenet_mqtt_h_
#define __onenet_mqtt_h_
#include "esp_err.h"
#include "cJSON.h"
#include "GPS.h"
// ===== 写死的设备名称 =====
#define MY_DEVICE_NAME       "cam_dht11"      // S3自己的设备名
#define PRODUCT_ID           "5OFAy8Z8N9"
#define PRODUCT_ACCESS_KEY   "UGhrVFREWnQzaUkxTXl4SW8xMmk0Q01WbXFsbHM5REE="


esp_err_t onenet_start(void);
static void onenet_property_ack(const char* id, int code, const char* msg);
void onenet_subscribe(void);
esp_err_t onenet_post_property_data(const char*data);

// 自定义Topic设备间通信接口
void onenet_publish_to_device(const char* target_device, const char* message);
void onenet_subscribe_custom_topics(void);
void onenet_register_message_callback(void (*callback)(const char* from_device, const char* message));

//任务上报函数
void onenet_start_report_task(void);

// 设备间消息回调函数
extern void (*device_message_callback)(const char* from_device, const char* message);
#endif