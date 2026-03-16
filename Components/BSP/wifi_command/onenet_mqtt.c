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
static esp_mqtt_client_handle_t mqtt_handle=NULL;

static TaskHandle_t report_task_handle = NULL;

#define TAG "onenet"
static void mqtt_event_handler(void *handler_args,esp_event_base_t base,int32_t event_id,void*event_data)
{
    esp_mqtt_event_handle_t event=event_data;
    switch((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
       ESP_LOGI(TAG,"MQTT_EVENT_CONNECTED!");
        onenet_subscribe();
        cJSON*property_js=onenet_property_upload();//为数据同步，上报数据
        char *data=cJSON_PrintUnformatted(property_js);
        onenet_post_property_data(data);
        cJSON_free(data);
        cJSON_Delete(property_js);

        //上传事件，不断更新数据
        onenet_start_report_task();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG,"MQTT_EVENT_DISCONNECTED!");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG,"MQTT_EVENT_SUBSCRIBED,msg_id=%d",event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG,"MQTT_EVENT_UNSUBSCRIBED,msg_id=%d",event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG,"MQTT_EVENT_PUBLISHED,msg_id=%d",event->msg_id);
        break;
    /*判断主题上下行*/
    case MQTT_EVENT_DATA:
{
    // 打印完整的主题
    char topic_str[256];
    int topic_len = event->topic_len < 255 ? event->topic_len : 255;
    memcpy(topic_str, event->topic, topic_len);
    topic_str[topic_len] = '\0';
    
    // 打印完整的数据
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
        
        cJSON* property = cJSON_Parse(event->data);
       cJSON *property_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(property_js,"id");

                onenet_property_handle(property_js);
                onenet_property_ack(cJSON_GetStringValue(id_js),200,"success");
                cJSON_Delete(property_js);
    }
    break;
}
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG,"MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG,"other event id:%d",event->event_id);
        break;
    }
}

esp_err_t onenet_start(void)
{
    esp_mqtt_client_config_t mqtt_config;
    memset(&mqtt_config,0,sizeof(mqtt_config));
  
    mqtt_config.broker.address.uri= "mqtt://mqtts.heclouds.com";
    mqtt_config.broker.address.port=1883;
    
    mqtt_config.credentials.client_id=ONENET_device_NAME;
    mqtt_config.credentials.username=ONENET_PRODUCT_ID;
    
    static char token[256];
   /*一定要写成NULL，不然连接不了，大坑！ */
   /*2074859482时间戳，时间大概是2027-01-23 19:27:10*/
    dev_token_generate(token,SIG_METHOD_SHA256,2074859482,ONENET_PRODUCT_ID,NULL,ONENET_PRODUCT_ACCESS_KEY);
    mqtt_config.credentials.authentication.password=token;
    mqtt_handle=esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(mqtt_handle,ESP_EVENT_ANY_ID,mqtt_event_handler,NULL);
    return esp_mqtt_client_start(mqtt_handle);
}

/*回应json的ack函数*/
static void onenet_property_ack(const char* id, int code, const char* msg)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply", 
             ONENET_PRODUCT_ID, ONENET_device_NAME);
    
    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js, "id", id);
    cJSON_AddNumberToObject(reply_js, "code", code);
    cJSON_AddStringToObject(reply_js, "msg", msg);  // 注意：是"msg"！
    
    char* data = cJSON_PrintUnformatted(reply_js);
    
    // 打印调试信息
    ESP_LOGI(TAG, "=== Sending ACK ===");
    ESP_LOGI(TAG, "Topic: %s", topic);
    ESP_LOGI(TAG, "Data: %s", data);
    
    int msg_id = esp_mqtt_client_publish(mqtt_handle, topic, data, strlen(data), 1, 0);
    ESP_LOGI(TAG, "Publish msg_id: %d", msg_id);
    
    free(data);
    cJSON_Delete(reply_js);
}

/*订阅主题函数*/

void onenet_subscribe(void)
{
    char topic[128];
    
    // 原来的订阅
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set", 
             ONENET_PRODUCT_ID, ONENET_device_NAME);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);
    ESP_LOGI(TAG, "Subscribed to specific: %s", topic);
    
    // 添加通配符，订阅所有主题（调试用）
    snprintf(topic, sizeof(topic), "$sys/%s/%s/#", 
             ONENET_PRODUCT_ID, ONENET_device_NAME);
    int msg_id = esp_mqtt_client_subscribe_single(mqtt_handle, topic, 0);
    ESP_LOGI(TAG, "Subscribed to wildcard: %s, msg_id=%d", topic, msg_id);
}
/*上传代码的*/
esp_err_t onenet_post_property_data(const char*data)
{
    char topic[128];
    snprintf(topic,128,"$sys/%s/%s/thing/property/post",ONENET_PRODUCT_ID,ONENET_device_NAME);
    ESP_LOGI(TAG,"upload topic:%s,payload:%s",topic,data);
    return esp_mqtt_client_publish(mqtt_handle,topic,data,strlen(data),1,0);

}
/*后面修改成GPS的参数*/
//上报任务函数,
static void report_task(void *pvParameters)
{
    while (1) {
        // 等待MQTT连接建立
        if (mqtt_handle != NULL) {
            // 读取DHT11
            dht11_read_data(GPIO_NUM_15);
            
            // 上报数据
            cJSON *property_js = onenet_property_upload();
            char *data = cJSON_PrintUnformatted(property_js);
            
            ESP_LOGI(TAG, "Periodic report: %s", data);
            onenet_post_property_data(data);
            
            cJSON_free(data);
            cJSON_Delete(property_js);
        }
        
        // 每1秒上报一次
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