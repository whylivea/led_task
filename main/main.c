#include "LED.h"
#include <stdio.h>  
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"//
#include "freertos/task.h"//
#include "Usuart.h"
#include "oled.h"
#include "wifi_command.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "onenet_token.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include "driver\ledc.h"
#include "dht11.h"
#define id "a405"
#define password "DZCXXH666666"

static EventGroupHandle_t wifi_event=NULL;
#define WIFI_CONNECT_BIT BIT0
static void wifi_state_callback(wifi_state state)
{
    if(state== wifi_connect_able)
    {
        xEventGroupSetBits(wifi_event,WIFI_CONNECT_BIT);
    }

}

void app_main(void)
{ 
   nvs_flash_init();
   wifi_event=xEventGroupCreate();
   DHT11_Init(GPIO_NUM_15);
   onenet_dm_Init();
   LED_Init(2);
   wifi_command_Init(wifi_state_callback);
   wifi_command_connect(id,password);
   EventBits_t ev;
   while(1)
   {
     dht11_read_data(GPIO_NUM_15);
     ev=xEventGroupWaitBits(wifi_event,WIFI_CONNECT_BIT,pdTRUE,pdFALSE,pdMS_TO_TICKS(10*1000));
     if(ev&WIFI_CONNECT_BIT)
    {
      onenet_start();  
    }
    }
}