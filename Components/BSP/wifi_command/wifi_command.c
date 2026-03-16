#include "wifi_command.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "stdio.h"
#include "esp_netif.h"
static p_wifi_state_cb WIFI_callback=NULL;//定义函数指针类型
#define MAX_CONNECT_REPTY 10
static int connect_repty=0;
#define TAG "WIFI_manager"
/*回调函数主体，是p_wifi_state_cb的真正函数指针主体 */
void wifi_state_handler(wifi_state state)//回调函数，用来判断连接状态
{
    if(state==wifi_connect_disable)
    {
        ESP_LOGI(TAG,"wifi connect disable!");
        
    }
    else if(state==wifi_connect_able)
    {
        ESP_LOGI(TAG,"wifi connect successful!");
       
    }
}
//当前sta连接状态
static bool is_sta_connected=false;
/**/
static void event_handler(void *arg,esp_event_base_t event_base,
                           int32_t event_id,void* event_data)
{
    if(event_base==WIFI_EVENT)
    {
    switch(event_id)
    { 
        case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
        case WIFI_EVENT_STA_DISCONNECTED:
        if(is_sta_connected)
        {
            is_sta_connected=false;
            if(WIFI_callback)
            {
                WIFI_callback(wifi_connect_disable);
            }
        }
        if(connect_repty<MAX_CONNECT_REPTY)
        {
        esp_wifi_connect();
        connect_repty++;
        } 
        break;
        case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG,"connect to ap");
    }

    }
    if(event_base==IP_EVENT)
    {
        if(event_id==IP_EVENT_STA_GOT_IP)
        {
        ESP_LOGI(TAG,"GET ip addr");
        is_sta_connected=true;
        if(WIFI_callback)
            WIFI_callback(wifi_connect_able);
        }

    }
}


void wifi_command_Init(p_wifi_state_cb f)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();//设置成sta模式
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,//需要event_hander回调函数
                                                        NULL,
                                                        NULL     
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,//需要event_hander回调函数
                                                        NULL,
                                                        NULL

    ));
    WIFI_callback=f;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());     
}

void  wifi_command_connect(const char *ssid,const char *password)
{
    wifi_config_t wifi_config={
        .sta={
            .threshold.authmode=WIFI_AUTH_WPA2_PSK,
            },
        };
    snprintf((char *)wifi_config.sta.ssid,32,"%s",ssid);//将我们输入的id赋值到sta结构体中的ssid
    snprintf((char *)wifi_config.sta.password,64,"%s",password);//将我们输入的password赋值到sta结构体的password
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));//将wifi模式设置成STA模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));//对wifi进行config
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if(mode!=WIFI_MODE_STA)
    {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
    }
    connect_repty=0;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));
    esp_wifi_start();
}
