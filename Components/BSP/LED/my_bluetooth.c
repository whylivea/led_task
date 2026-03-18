#include "my_bluetooth.h"
#include "esp_log.h"
#include "freertos/task.h"

// 蓝牙全局变量
static uint8_t target_bt_addr[6] = BT_TARGET_ADDR;
static bool bt_connected = false;
static uint32_t spp_handle = 0;
static uint8_t discovered_scn = 0;
static bool discovery_in_progress = false;

// 定时器句柄定义（非static，与头文件中的extern匹配）
TimerHandle_t reconnect_timer = NULL;

static const char *TAG = "my_bluetooth";

// 声明内部函数
static void spp_callback_handler(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);
static esp_err_t bluetooth_connect_to_target(void);

// ============ 蓝牙重连定时器回调 ============
void reconnect_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Reconnect timer triggered");
    bluetooth_connect_to_target();
}

// ============ 蓝牙SPP回调处理函数 ============
static void spp_callback_handler(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch(event) {
        case ESP_SPP_INIT_EVT:
            ESP_LOGI(TAG, "ESP_SPP_INIT_EVT");
            esp_bt_dev_set_device_name(BT_DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            
            vTaskDelay(pdMS_TO_TICKS(3000));
            ESP_LOGI(TAG, "Starting service discovery for CAM...");
            ESP_LOGI(TAG, "Target MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     target_bt_addr[0], target_bt_addr[1], target_bt_addr[2],
                     target_bt_addr[3], target_bt_addr[4], target_bt_addr[5]);
            
            discovery_in_progress = true;
            esp_err_t ret = esp_spp_start_discovery(target_bt_addr);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_spp_start_discovery failed: %s", esp_err_to_name(ret));
                discovery_in_progress = false;
            }
            break;

        case ESP_SPP_DISCOVERY_COMP_EVT:
            discovery_in_progress = false;
            if (param->disc_comp.status == ESP_SPP_SUCCESS) {
                ESP_LOGI(TAG, "Service discovery success, found %d services", 
                         param->disc_comp.scn_num);
                
                for (int i = 0; i < param->disc_comp.scn_num; i++) {
                    ESP_LOGI(TAG, "  Service %d: SCN=%d", i, param->disc_comp.scn[i]);
                }
                
                if (param->disc_comp.scn_num > 0) {
                    discovered_scn = param->disc_comp.scn[0];
                    ESP_LOGI(TAG, "Connecting to CAM with SCN=%d", discovered_scn);
                    
                    esp_err_t ret = esp_spp_connect(ESP_SPP_SEC_NONE, 
                                                   ESP_SPP_ROLE_MASTER, 
                                                   discovered_scn, 
                                                   target_bt_addr);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(ret));
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        esp_spp_start_discovery(target_bt_addr);
                    }
                } else {
                    ESP_LOGW(TAG, "No SPP services found on CAM, retrying in 5s...");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    esp_spp_start_discovery(target_bt_addr);
                }
            } else {
                ESP_LOGE(TAG, "Service discovery failed, retrying in 5s...");
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_spp_start_discovery(target_bt_addr);
            }
            break;

        case ESP_SPP_OPEN_EVT:
            if (param->open.status == ESP_SPP_SUCCESS) {
                ESP_LOGI(TAG, "Connected to CAM successfully! handle: %lu", 
                         (unsigned long)param->open.handle);
                bt_connected = true;
                spp_handle = param->open.handle;
                
                uint8_t keep_alive = 0;
                esp_spp_write(spp_handle, 1, &keep_alive);
                
                if (reconnect_timer) xTimerStop(reconnect_timer, 0);
                
                ESP_LOGI(TAG, "蓝牙连接已建立，可以发送指令");
            } else {
                ESP_LOGE(TAG, "Connection failed, status: %d", param->open.status);
                bt_connected = false;
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_spp_start_discovery(target_bt_addr);
            }
            break;

        case ESP_SPP_CLOSE_EVT:
            ESP_LOGI(TAG, "Disconnected from CAM");
            bt_connected = false;
            spp_handle = 0;
            if (reconnect_timer) {
                xTimerStart(reconnect_timer, 0);
            } else {
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_spp_start_discovery(target_bt_addr);
            }
            break;

        case ESP_SPP_WRITE_EVT:
            if (param->write.status == ESP_SPP_SUCCESS) {
                ESP_LOGI(TAG, "Data sent to CAM successfully, len=%d", param->write.len);
            }
            break;

        default:
            ESP_LOGI(TAG, "Unhandled SPP event: %d", event);
            break;
    }
}

// ============ 蓝牙初始化函数 ============
esp_err_t bluetooth_init(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "=== Bluetooth Initialization Start ===");
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "BT controller init success");
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "BT controller enabled");
    
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bluedroid init success");
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bluedroid enabled");
    
    ret = esp_spp_register_callback(spp_callback_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP callback register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPP callback registered");
    
    ret = esp_spp_init(ESP_SPP_MODE_CB);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPP init success");
    
    // 创建重连定时器
    reconnect_timer = xTimerCreate("reconnect_timer",
                                   pdMS_TO_TICKS(BT_RECONNECT_INTERVAL),
                                   pdFALSE,
                                   NULL,
                                   reconnect_timer_callback);
    
    ESP_LOGI(TAG, "=== Bluetooth Initialization Complete ===");
    return ESP_OK;
}

// ============ 连接到目标蓝牙设备 ============
static esp_err_t bluetooth_connect_to_target(void)
{
    if (bt_connected) {
        ESP_LOGI(TAG, "Already connected to CAM");
        return ESP_OK;
    }
    
    if (discovery_in_progress) {
        ESP_LOGW(TAG, "Discovery already in progress");
        return ESP_FAIL;
    }
    
    if (discovered_scn == 0) {
        ESP_LOGW(TAG, "SCN not discovered yet, starting discovery...");
        discovery_in_progress = true;
        return esp_spp_start_discovery(target_bt_addr);
    }
    
    esp_bd_addr_t bd_addr;
    memcpy(bd_addr, target_bt_addr, sizeof(esp_bd_addr_t));
    
    ESP_LOGI(TAG, "Connecting to CAM with SCN=%d", discovered_scn);
    return esp_spp_connect(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_MASTER, 
                          discovered_scn, bd_addr);
}

// ============ 通过蓝牙发送字符 ============
esp_err_t bluetooth_send_char(char c)
{
    if (!bt_connected || spp_handle == 0) {
        ESP_LOGW(TAG, "Bluetooth not connected, cannot send '%c'", c);
        bluetooth_connect_to_target();
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_spp_write(spp_handle, 1, (uint8_t*)&c);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sent '%c' to CAM via Bluetooth", c);
    } else {
        ESP_LOGE(TAG, "Failed to send '%c': %s", c, esp_err_to_name(ret));
    }
    return ret;
}

// ============ 温度监控任务 ============
void temp_monitor_task(void *pvParameters)
{
    float temp_threshold = 30.0;
    int send_count = 0;
    int fail_count = 0;
    
    while(1) {
        if (dht11_read_data(GPIO_NUM_15) == ESP_OK && dht11_data.valid) {
            float temperature = dht11_data.temperature_int + dht11_data.temperature_dec / 10.0;
            
            if (temperature > temp_threshold) {
                ESP_LOGI(TAG, "Temp %.1f°C > %d°C", temperature, (int)temp_threshold);
                esp_err_t ret = bluetooth_send_char('a');
                if (ret == ESP_OK) {
                    send_count++;
                    ESP_LOGI(TAG, "Alert sent %d times", send_count);
                    fail_count = 0;
                } else {
                    fail_count++;
                    ESP_LOGW(TAG, "Failed to send alert, fail count: %d", fail_count);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}