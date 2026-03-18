#include "onenet_mqtt.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "esp_log.h"
#include "onenet_dm.h"
#include "driver/gpio.h"
#include "dht11.h"
#include "esp_log.h"

// 蓝牙发送函数声明
extern esp_err_t bluetooth_send_char(char c);

static esp_mqtt_client_handle_t mqtt_handle = NULL;
static TaskHandle_t report_task_handle = NULL;

// 设备间消息回调函数指针
 void (*device_message_callback)(const char* from_device, const char* message) = NULL;

// 温度阈值定义
#define TEMPERATURE_THRESHOLD 30

// ===== 写死的设备名称 =====
#define MY_DEVICE_NAME       "cam_dht11"      // S3自己的设备名
#define TARGET_DEVICE_NAME   "aaaaaaaaaaa"    // CAM的设备名
#define PRODUCT_ID           "5OFAy8Z8N9"
#define PRODUCT_ACCESS_KEY   "UGhrVFREWnQzaUkxTXl4SW8xMmk0Q01WbXFsbHM5REE="

#define TAG "onenet"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED!");
        onenet_subscribe();
        
        cJSON* property_js = onenet_property_upload();
        char *data = cJSON_PrintUnformatted(property_js);
        onenet_post_property_data(data);
        cJSON_free(data);
        cJSON_Delete(property_js);

        // 上传事件，不断更新数据
        onenet_start_report_task();
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED!");
        break;
        
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
        
    case MQTT_EVENT_DATA:
    {
        char topic_str[256];
        int topic_len = event->topic_len < 255 ? event->topic_len : 255;
        memcpy(topic_str, event->topic, topic_len);
        topic_str[topic_len] = '\0';

        char data_str[512];
        int data_len = event->data_len < 511 ? event->data_len : 511;
        memcpy(data_str, event->data, data_len);
        data_str[data_len] = '\0';

        ESP_LOGI(TAG, "=== MQTT Message Received ===");
        ESP_LOGI(TAG, "Topic: %s", topic_str);
        ESP_LOGI(TAG, "Data: %s", data_str);

        // 检查是否是属性设置
        if (strstr(topic_str, "property/set"))
        {
            ESP_LOGI(TAG, "!!! PROPERTY SET COMMAND !!!");
            
            cJSON *property_js = cJSON_Parse(event->data);
            cJSON *id_js = cJSON_GetObjectItem(property_js, "id");

            onenet_property_handle(property_js);
            onenet_property_ack(cJSON_GetStringValue(id_js), 200, "success");
            cJSON_Delete(property_js);
        }
        // 检查是否是设备间通信消息
        else if (strstr(topic_str, "/device/") && strstr(topic_str, "/command"))
        {
            ESP_LOGI(TAG, "!!! DEVICE-TO-DEVICE MESSAGE !!!");

            // 解析发送方设备名称
            char from_device[64] = {0};
            char to_device[64] = {0};

            char* sys_ptr = strstr(topic_str, "$sys/");
            if (sys_ptr) {
                char* product_end = strchr(sys_ptr + 5, '/');
                if (product_end) {
                    char* from_start = product_end + 1;
                    char* from_end = strchr(from_start, '/');
                    if (from_end) {
                        strncpy(from_device, from_start, from_end - from_start);

                        char* device_ptr = strstr(from_end, "/device/");
                        if (device_ptr) {
                            char* to_start = device_ptr + 8;
                            char* to_end = strchr(to_start, '/');
                            if (to_end) {
                                strncpy(to_device, to_start, to_end - to_start);

                                ESP_LOGI(TAG, "From: %s, To: %s", from_device, to_device);

                                // 检查是否是发给本设备的消息
                                if (strcmp(to_device, MY_DEVICE_NAME) == 0) {
                                    ESP_LOGI(TAG, "This is a message for me!");

                                    if (device_message_callback != NULL) {
                                        device_message_callback(from_device, data_str);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        break;
    }
    
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
        
    default:
        ESP_LOGI(TAG, "other event id:%d", event->event_id);
        break;
    }
}

esp_err_t onenet_start(void)
{
    esp_mqtt_client_config_t mqtt_config;
    memset(&mqtt_config, 0, sizeof(mqtt_config));
    
    mqtt_config.broker.address.uri = "mqtt://mqtts.heclouds.com";
    mqtt_config.broker.address.port = 1883;
    
    mqtt_config.credentials.client_id = MY_DEVICE_NAME;
    mqtt_config.credentials.username = PRODUCT_ID;
    
    static char token[256];
    dev_token_generate(token, SIG_METHOD_SHA256, 2074859482, PRODUCT_ID, NULL, PRODUCT_ACCESS_KEY);
    mqtt_config.credentials.authentication.password = token;
    
    mqtt_handle = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    return esp_mqtt_client_start(mqtt_handle);
}

/* 回应json的ack函数 */
static void onenet_property_ack(const char* id, int code, const char* msg)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply", 
             PRODUCT_ID, MY_DEVICE_NAME);
    
    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js, "id", id);
    cJSON_AddNumberToObject(reply_js, "code", code);
    cJSON_AddStringToObject(reply_js, "msg", msg);
    
    char* data = cJSON_PrintUnformatted(reply_js);
    
    ESP_LOGI(TAG, "=== Sending ACK ===");
    ESP_LOGI(TAG, "Topic: %s", topic);
    ESP_LOGI(TAG, "Data: %s", data);
    
    int msg_id = esp_mqtt_client_publish(mqtt_handle, topic, data, strlen(data), 1, 0);
    ESP_LOGI(TAG, "Publish msg_id: %d", msg_id);
    
    free(data);
    cJSON_Delete(reply_js);
}

/* 订阅主题函数 */
void onenet_subscribe(void)
{
    char topic[128];
    
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set", 
             PRODUCT_ID, MY_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);
    ESP_LOGI(TAG, "Subscribed to property set: %s", topic);
    
    // 订阅所有发给本设备的设备间消息
    snprintf(topic, sizeof(topic), "$sys/%s/+/device/%s/command", 
             PRODUCT_ID, MY_DEVICE_NAME);
    int msg_id = esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);
    ESP_LOGI(TAG, "Subscribed to device messages: %s, msg_id=%d", topic, msg_id);
}

/* 上传属性数据 */
esp_err_t onenet_post_property_data(const char* data)
{
    char topic[128];
    snprintf(topic, 128, "$sys/%s/%s/thing/property/post", 
             PRODUCT_ID, MY_DEVICE_NAME);
    ESP_LOGI(TAG, "upload topic:%s, payload:%s", topic, data);
    return esp_mqtt_client_publish(mqtt_handle, topic, data, strlen(data), 1, 0);
}

/* 发布消息到CAM */
void onenet_publish_to_cam(const char* message)
{
    if (mqtt_handle == NULL) {
        ESP_LOGE(TAG, "MQTT not initialized");
        return;
    }
    
    char topic[256];
    // 写死的Topic格式：$sys/产品ID/本设备/device/目标设备/command
    snprintf(topic, sizeof(topic), "$sys/%s/%s/device/%s/command",
             PRODUCT_ID, MY_DEVICE_NAME, TARGET_DEVICE_NAME);
    
    ESP_LOGI(TAG, "Publishing to CAM: %s", topic);
    ESP_LOGI(TAG, "Message: %s", message);
    
    int msg_id = esp_mqtt_client_publish(mqtt_handle, topic, message, strlen(message), 1, 0);
    ESP_LOGI(TAG, "Publish msg_id: %d", msg_id);
}

/* 注册设备间消息回调函数 */
void onenet_register_message_callback(void (*callback)(const char* from_device, const char* message))
{
    device_message_callback = callback;
    ESP_LOGI(TAG, "Device message callback registered");
}

/* 上报任务函数 */
static void report_task(void *pvParameters)
{
    while (1) {
        if (mqtt_handle != NULL) {
            // 读取DHT11
            dht11_read_data(GPIO_NUM_15);

            // 检查温度是否超过阈值
            int temperature = dht11_data.temperature_int + dht11_data.temperature_dec * 0.1;
            if (temperature > TEMPERATURE_THRESHOLD) {
                ESP_LOGI(TAG, "Temperature %d exceeds threshold %d, sending alert!",
                         temperature, TEMPERATURE_THRESHOLD);

                // 发送pitch消息给CAM
                char message[128];
                snprintf(message, sizeof(message), "{\"type\":\"pitch\",\"temperature\":%d}", temperature);
                onenet_publish_to_cam(message);

                // 通过蓝牙发送字符'a'
                esp_err_t ret = bluetooth_send_char('a');
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send Bluetooth alert");
                } else {
                    ESP_LOGI(TAG, "Bluetooth alert sent successfully");
                }
            }

            // 上报数据到OneNet
            cJSON *property_js = onenet_property_upload();
            char *data = cJSON_PrintUnformatted(property_js);

            ESP_LOGI(TAG, "Periodic report: %s", data);
            onenet_post_property_data(data);

            cJSON_free(data);
            cJSON_Delete(property_js);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 启动上报任务的函数
void onenet_start_report_task(void)
{
    if (report_task_handle == NULL) {
        xTaskCreate(report_task, "report_task", 4096, NULL, 5, &report_task_handle);
        ESP_LOGI(TAG, "Report task started");
    }
}