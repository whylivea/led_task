#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "JGdistance.h"
#include "wifi_command.h"
#include "onenet_token.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"
#include "GPS.h"
#include "dht11.h"
#include "my_bluetooth.h"
#include "serial.h"


#define id "a405"
#define password "DZCXXH666666"

static EventGroupHandle_t wifi_event = NULL;
#define WIFI_CONNECT_BIT BIT0

static void wifi_state_callback(wifi_state state)
{
    if(state == wifi_connect_able)
        xEventGroupSetBits(wifi_event, WIFI_CONNECT_BIT);
}

// UART定义
#define UART_VL53L0    UART_NUM_1  // VL53L0: TX=14, RX=13
// 语音模块使用软件串口，不需要硬件UART定义

#define TAG "MAIN"

// 软件串口实例ID
static int voice_serial_id = -1;

// 函数声明
void init_vl53l0_uart(void);
void init_voice_uart(void);
void send_to_voice(char c);

// ============ 初始化VL53L0的UART ============
void init_vl53l0_uart(void) {
    ESP_LOGI(TAG, "Init VL53L0 UART: TX=14, RX=13");
    
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    uart_param_config(UART_VL53L0, &uart_config);
    uart_set_pin(UART_VL53L0, 14, 13, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_VL53L0, 1024 * 2, 0, 0, NULL, 0);
    
    ESP_LOGI(TAG, "VL53L0 UART ready");
}

// ============ 初始化语音模块的软件串口 ============
void init_voice_uart(void) {
    ESP_LOGI(TAG, "Init Voice Software Serial: TX=25, RX=26");

    // 使用软件串口库初始化
    voice_serial_id = serial_begin(9600, 26, 25);
    if (voice_serial_id < 0) {
        ESP_LOGE(TAG, "Failed to initialize software serial!");
        return;
    }

    ESP_LOGI(TAG, "Voice Software Serial ready (instance_id=%d)", voice_serial_id);
}

// ============ 发送字符到语音模块 ============
void send_to_voice(char c) {
    if (voice_serial_id < 0) {
        ESP_LOGE("VOICE", "Software serial not initialized!");
        return;
    }

    serial_write(voice_serial_id, (uint8_t)c);
    ESP_LOGI("VOICE", "Sent: %c", c);
}

// ============ 主函数 ============
void app_main(void) {
    
    // 2. 初始化两个UART
    init_vl53l0_uart();   // VL53L0测距模块
    init_voice_uart();    // 语音模块
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
     wifi_event = xEventGroupCreate();
    
   DHT11_Init(GPIO_NUM_15);
    onenet_dm_Init();
    wifi_command_Init(wifi_state_callback);
    ESP_ERROR_CHECK(ret);  
    // 3. 等待硬件稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    // 5. 启动VL53L0任务
    xTaskCreate(vl53l0_task, "vl53l0", 4096, NULL, 5, NULL);
    bluetooth_init();
    
    reconnect_timer = xTimerCreate("reconnect_timer",
                                   pdMS_TO_TICKS(BT_RECONNECT_INTERVAL),
                                   pdFALSE, NULL,
                                   reconnect_timer_callback);
    
        //
    xTaskCreate(temp_monitor_task, "temp_monitor", 4096, NULL, 5, NULL);
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