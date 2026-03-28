#include "my_bluetooth.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "mpu6050.h"
#include <math.h>
#include "LED.h"

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

// ============ MPU6050姿态角监控任务 ============
void temp_monitor_task(void *pvParameters)
{
    // 倾斜角阈值设置（单位：度）
    const float tilt_threshold = 30.0f;

    // LED GPIO定义
    const uint8_t LEFT_LED_PIN = GPIO_NUM_33;
    const uint8_t RIGHT_LED_PIN = GPIO_NUM_32;

    // LED闪烁参数
    const TickType_t blink_interval = pdMS_TO_TICKS(200);
    TickType_t last_blink_time = 0;
    bool led_state = false;

    // 拍照状态变量
    int photo_count = 0;
    bool is_capturing = false;
    TickType_t capture_start_time = 0;
    const TickType_t cooldown_time = pdMS_TO_TICKS(5000);
    TickType_t last_capture_time = 0;

    // LED激活状态
    bool left_led_active = false;
    bool right_led_active = false;
    float last_tilt_x = 0.0f;

    ESP_LOGI(TAG, "MPU6050姿态监控任务启动");
    ESP_LOGI(TAG, "倾斜角阈值: %.1f度", tilt_threshold);
    ESP_LOGI(TAG, "左LED GPIO: %d, 右LED GPIO: %d", LEFT_LED_PIN, RIGHT_LED_PIN);

    // 初始化LED（即使没有MPU6050，LED也可以先初始化）
    LED_Init(LEFT_LED_PIN);
    LED_Init(RIGHT_LED_PIN);
    gpio_set_level(LEFT_LED_PIN, 0);
    gpio_set_level(RIGHT_LED_PIN, 0);

    // MPU6050相关变量
    mpu6050_config_t config = {
        .address = MPU6050_DEFAULT_ADDRESS,
        .accel_range = MPU6050_ACCEL_RANGE_2_G,
        .gyro_range = MPU6050_GYRO_RANGE_250_DPS,
        .sample_rate = 100
    };
    mpu6050_data_t sensor_data;
    bool mpu6050_ok = false;

    // 尝试初始化MPU6050，失败也不退出，只是标记为不可用
    esp_err_t ret = mpu6050_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050初始化失败: %s，将禁用姿态检测功能", esp_err_to_name(ret));
        mpu6050_ok = false;
    } else {
        ESP_LOGI(TAG, "MPU6050初始化成功，姿态检测功能已启用");
        mpu6050_ok = true;
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待传感器稳定
    }

    // 主循环
    while(1) {
        // 只有在MPU6050正常时才读取数据
        if (mpu6050_ok) {
            ret = mpu6050_read_all(&sensor_data);

            if (ret == ESP_OK) {
                // 使用加速度计数据计算倾斜角
                float accel_x_g = sensor_data.ax / 16384.0f;
                float accel_y_g = sensor_data.ay / 16384.0f;
                float accel_z_g = sensor_data.az / 16384.0f;

                // 计算X轴方向的倾斜角（用于左右转头检测）
                float tilt_x = atan2(accel_x_g, sqrt(accel_y_g * accel_y_g + accel_z_g * accel_z_g)) * 180.0f / M_PI;

                // 计算总体倾斜角（用于摔倒检测）
                float tilt_angle = atan2(sqrt(accel_x_g * accel_x_g + accel_y_g * accel_y_g), accel_z_g) * 180.0f / M_PI;

                ESP_LOGD(TAG, "加速度: X=%.2fg, Y=%.2fg, Z=%.2fg, X轴倾斜=%.1f度, 总倾斜角=%.1f度",
                         accel_x_g, accel_y_g, accel_z_g, tilt_x, tilt_angle);

                // 检查左右转头动作
                if (tilt_x < -tilt_threshold && !left_led_active) {
                    ESP_LOGI(TAG, "检测到向左转头: %.1f度", tilt_x);
                    left_led_active = true;
                    right_led_active = false;
                    last_blink_time = xTaskGetTickCount();
                    last_tilt_x = tilt_x;
                }
                else if (tilt_x > tilt_threshold && !right_led_active) {
                    ESP_LOGI(TAG, "检测到向右转头: %.1f度", tilt_x);
                    right_led_active = true;
                    left_led_active = false;
                    last_blink_time = xTaskGetTickCount();
                    last_tilt_x = tilt_x;
                }
                else if (fabs(tilt_x) < tilt_threshold * 0.5f) {
                    if (left_led_active || right_led_active) {
                        ESP_LOGI(TAG, "回到中间位置，关闭LED");
                        left_led_active = false;
                        right_led_active = false;
                        gpio_set_level(LEFT_LED_PIN, 0);
                        gpio_set_level(RIGHT_LED_PIN, 0);
                    }
                }

                // LED闪烁处理
                if (left_led_active || right_led_active) {
                    TickType_t current_time = xTaskGetTickCount();
                    if ((current_time - last_blink_time) >= blink_interval) {
                        led_state = !led_state;
                        last_blink_time = current_time;

                        if (left_led_active) {
                            gpio_set_level(LEFT_LED_PIN, led_state ? 1 : 0);
                            gpio_set_level(RIGHT_LED_PIN, 0);
                        } else if (right_led_active) {
                            gpio_set_level(RIGHT_LED_PIN, led_state ? 1 : 0);
                            gpio_set_level(LEFT_LED_PIN, 0);
                        }
                    }
                }

                // 检查是否达到总倾斜角阈值（摔倒检测）
                if (tilt_angle > tilt_threshold * 1.5f && !is_capturing) {
                    TickType_t current_time = xTaskGetTickCount();
                    if ((current_time - last_capture_time) >= cooldown_time) {
                        ESP_LOGI(TAG, "检测到摔倒风险: 总倾斜角 %.1f度", tilt_angle);

                        is_capturing = true;
                        photo_count = 0;
                        capture_start_time = current_time;

                        left_led_active = false;
                        right_led_active = false;
                        gpio_set_level(LEFT_LED_PIN, 0);
                        gpio_set_level(RIGHT_LED_PIN, 0);

                        bluetooth_send_char('s');  // 用's'表示摔倒，避免冲突
                    }
                }

                // 处理拍照流程
                if (is_capturing) {
                    TickType_t current_time = xTaskGetTickCount();
                    if (photo_count < 2 && (current_time - capture_start_time) >= pdMS_TO_TICKS(500 * (photo_count + 1))) {
                        bluetooth_send_char('s');
                        photo_count++;
                        ESP_LOGI(TAG, "已拍摄 %d/3 张照片", photo_count + 1);
                    } else if (photo_count >= 2) {
                        is_capturing = false;
                        last_capture_time = current_time;
                        ESP_LOGI(TAG, "拍照流程完成");
                    }
                }
            } else {
                ESP_LOGE(TAG, "读取MPU6050数据失败: %s", esp_err_to_name(ret));
                // 如果连续失败，标记为不可用
                static int fail_count = 0;
                fail_count++;
                if (fail_count > 10) {
                    ESP_LOGW(TAG, "MPU6050连续读取失败，禁用姿态检测");
                    mpu6050_ok = false;
                }
            }
        } else {
            // MPU6050不可用，静默等待，不输出日志
            // 每10秒尝试重新初始化一次
            static TickType_t last_check = 0;
            TickType_t now = xTaskGetTickCount();
            if ((now - last_check) >= pdMS_TO_TICKS(10000)) {
                last_check = now;
                ESP_LOGI(TAG, "尝试重新初始化MPU6050...");
                ret = mpu6050_init(&config);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "MPU6050重新初始化成功！");
                    mpu6050_ok = true;
                }
            }
        }

        // 每100ms检查一次（10Hz）
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}