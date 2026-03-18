#include "LED.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "Usuart.h"
#include "oled.h"
#include "wifi_command.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "onenet_token.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include "driver/ledc.h"
#include "dht11.h"
#include "GPS.h"
#include "my_bluetooth.h"




#define id "a405"
#define password "DZCXXH666666"

static EventGroupHandle_t wifi_event = NULL;
#define WIFI_CONNECT_BIT BIT0

#define TAG "MAIN"

static TaskHandle_t uart_task_handle = NULL;
// ============ WiFi状态回调 ============
static void wifi_state_callback(wifi_state state)
{
    if(state == wifi_connect_able)
        xEventGroupSetBits(wifi_event, WIFI_CONNECT_BIT);
}

// ============ 设备间消息回调 ============
static void device_message_handler(const char* from_device, const char* message)
{
    if (strstr(message, "\"type\":\"pitch\""))
        ESP_LOGI(TAG, "PITCH alert from %s", from_device);
}

//串口回调函数，接收a，串口2打印回a


static void uart_rx_task(void *pvParameters)
{
    uint8_t rx_data[128];
    int len;
    while(1) {
        // 读取串口数据
        len = uart_read_bytes(USART_US, rx_data, sizeof(rx_data), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            // 处理每个接收到的字节
            for (int i = 0; i < len; i++) {
                // 如果收到 'a'，回显 'a'
                if (rx_data[i] == 'a') {
                    ESP_LOGI(TAG, "Received 'a', echoing back");
                    uart_write_bytes(USART_US, "a", 1);
                }
                // 如果收到 'A'，回显 'A'
                else if (rx_data[i] == 'A') {

                    uart_write_bytes(USART_US, "A", 1);
                }
                // 其他字符，回显原字符
                else {
                    uart_write_bytes(USART_US, &rx_data[i], 1);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}



// ============ 主函数 ============
void app_main(void)
{


    
   esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    wifi_event = xEventGroupCreate();
    
    DHT11_Init(GPIO_NUM_15);
   onenet_register_message_callback(device_message_handler);
    onenet_dm_Init();
    LED_Init(2);
    wifi_command_Init(wifi_state_callback);
    
    ESP_LOGI(TAG, "Initializing Bluetooth...");
    bluetooth_init();
    
    reconnect_timer = xTimerCreate("reconnect_timer",
                                   pdMS_TO_TICKS(BT_RECONNECT_INTERVAL),
                                   pdFALSE, NULL,
                                   reconnect_timer_callback);
    
    xTaskCreate(temp_monitor_task, "temp_monitor", 4096, NULL, 5, NULL);
     xTaskCreate(uart_rx_task, "uart_rx", 3072, NULL, 5, &uart_task_handle);
    wifi_command_connect(id, password);
    start_gps_task();
    while(1) {
        EventBits_t ev = xEventGroupWaitBits(wifi_event, WIFI_CONNECT_BIT, 
                                             pdTRUE, pdFALSE, pdMS_TO_TICKS(10*1000));
        if(ev & WIFI_CONNECT_BIT) {
            ESP_LOGI(TAG, "WiFi connected, starting OneNet...");
            onenet_start();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}