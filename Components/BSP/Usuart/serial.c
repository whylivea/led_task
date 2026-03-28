#include "serial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SW_SERIAL";

// 缓冲区大小
#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 1024
#define MAX_SERIAL_INSTANCES 3

// 串口实例结构
typedef struct {
    int rx_pin;              // 接收引脚
    int tx_pin;              // 发送引脚
    int baudrate;            // 波特率
    bool initialized;        // 是否已初始化

    // 发送缓冲区
    uint8_t tx_buf[TX_BUF_SIZE];
    int tx_len;
    TaskHandle_t tx_task_handle;
    TaskHandle_t parent_task_handle;

    // 接收缓冲区
    uint8_t rx_buf[RX_BUF_SIZE];
    int rx_len;
    int rx_save_idx;
    int rx_read_idx;
    TaskHandle_t rx_task_handle;
    SemaphoreHandle_t rx_mutex;  // 接收缓冲区互斥锁

} sw_serial_t;

// 全局实例数组
static sw_serial_t serial_instances[MAX_SERIAL_INSTANCES] = {0};

// 查找空闲实例
static sw_serial_t* find_free_instance(void) {
    for (int i = 0; i < MAX_SERIAL_INSTANCES; i++) {
        if (!serial_instances[i].initialized) {
            return &serial_instances[i];
        }
    }
    ESP_LOGE(TAG, "No free serial instance!");
    return NULL;
}

// 获取实例ID
static int get_instance_id(sw_serial_t *obj) {
    for (int i = 0; i < MAX_SERIAL_INSTANCES; i++) {
        if (&serial_instances[i] == obj) {
            return i;
        }
    }
    return -1;
}

// ============ 软件串口发送核心函数 ============

// 软件串口发送一个字节
static void sw_serial_send_byte(int tx_pin, uint8_t data, int baudrate) {
    int bit_time_us = 1000000 / baudrate;

    // 关中断，保证时序准确
    portDISABLE_INTERRUPTS();

    // 起始位：拉低
    gpio_set_level(tx_pin, 0);
    esp_rom_delay_us(bit_time_us);

    // 发送 8 个数据位（LSB 优先）
    for (int i = 0; i < 8; i++) {
        int bit = (data >> i) & 1;
        gpio_set_level(tx_pin, bit);
        esp_rom_delay_us(bit_time_us);
    }

    // 停止位：拉高
    gpio_set_level(tx_pin, 1);
    esp_rom_delay_us(bit_time_us);

    portENABLE_INTERRUPTS();
}

// ============ 软件串口接收核心函数 ============

// 软件串口接收一个字节（使用超时机制）
static int sw_serial_recv_byte_timeout(int rx_pin, int baudrate, uint8_t *data, int timeout_ms) {
    int bit_time_us = 1000000 / baudrate;
    int half_bit_us = bit_time_us / 2;

    // 使用 TickType_t 实现超时
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    // 等待起始位（下降沿）
    while (gpio_get_level(rx_pin) == 1) {
        // 检查超时
        if ((xTaskGetTickCount() - start_tick) > timeout_ticks) {
            return 0;  // 超时
        }
        taskYIELD();  // 让出CPU，避免看门狗超时
    }

    // 等待到起始位的中间
    esp_rom_delay_us(half_bit_us);

    // 读取 8 个数据位
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        esp_rom_delay_us(bit_time_us);
        int bit = gpio_get_level(rx_pin);
        if (bit) {
            byte |= (1 << i);
        }
    }

    // 等待停止位
    esp_rom_delay_us(bit_time_us);

    // 验证停止位
    if (gpio_get_level(rx_pin) != 1) {
        ESP_LOGD(TAG, "Stop bit error");
        return 0;
    }

    *data = byte;
    return 1;
}

// ============ 任务函数 ============

// 软件串口接收任务
static void sw_rx_task(void *pvParameters) {
    sw_serial_t *obj = (sw_serial_t *)pvParameters;
    int instance_id = get_instance_id(obj);
    ESP_LOGI(TAG, "RX task started: instance=%d, pin=%d, baud=%d", instance_id, obj->rx_pin, obj->baudrate);

    obj->rx_len = 0;
    obj->rx_save_idx = 0;
    obj->rx_read_idx = 0;

    while (1) {
        uint8_t data;
        // 使用超时机制接收字节，避免看门狗超时
        if (sw_serial_recv_byte_timeout(obj->rx_pin, obj->baudrate, &data, 100)) {
            // 获取互斥锁，保护接收缓冲区
            if (xSemaphoreTake(obj->rx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                // 检查缓冲区是否已满
                if (obj->rx_len < RX_BUF_SIZE) {
                    obj->rx_buf[obj->rx_save_idx] = data;
                    obj->rx_len++;
                    obj->rx_save_idx = (obj->rx_save_idx + 1) % RX_BUF_SIZE;
                    ESP_LOGD(TAG, "RX instance=%d: 0x%02X (%c)", instance_id, data, data);
                } else {
                    ESP_LOGW(TAG, "RX buffer overflow! instance=%d", instance_id);
                }
                xSemaphoreGive(obj->rx_mutex);
            }
        } else {
            // 接收超时，让出CPU时间
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// 软件串口发送任务
static void sw_tx_task(void *pvParameters) {
    sw_serial_t *obj = (sw_serial_t *)pvParameters;
    ESP_LOGI(TAG, "TX task started: pin=%d, baud=%d", obj->tx_pin, obj->baudrate);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // 发送数据
        for (int i = 0; i < obj->tx_len; i++) {
            sw_serial_send_byte(obj->tx_pin, obj->tx_buf[i], obj->baudrate);
        }

        obj->tx_len = 0;
        xTaskNotifyGive(obj->parent_task_handle);
    }
}

// ============ 对外 API ============

// 初始化软件串口
int serial_begin(int baudrate, int rx_pin, int tx_pin) {
    sw_serial_t *obj = find_free_instance();
    if (!obj) {
        ESP_LOGE(TAG, "No free serial instance!");
        return -1;
    }

    // 初始化结构体
    memset(obj, 0, sizeof(sw_serial_t));
    obj->rx_pin = rx_pin;
    obj->tx_pin = tx_pin;
    obj->baudrate = baudrate;
    obj->initialized = true;

    // 创建接收缓冲区互斥锁
    obj->rx_mutex = xSemaphoreCreateMutex();
    if (obj->rx_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        obj->initialized = false;
        return -1;
    }

    // 配置 TX 引脚
    gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(tx_pin, 1);  // 空闲状态高电平

    // 配置 RX 引脚（如果需要接收）
    if (rx_pin >= 0) {
        gpio_set_direction(rx_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(rx_pin, GPIO_PULLUP_ONLY);
    }

    // 创建任务
    UBaseType_t prio = uxTaskPriorityGet(NULL);
    if (prio != configMAX_PRIORITIES) prio++;

    obj->parent_task_handle = xTaskGetCurrentTaskHandle();

    // 只有 RX 引脚有效时才创建接收任务
    if (rx_pin >= 0) {
        if (xTaskCreate(sw_rx_task, "sw_rx_task", 4096, obj, prio, &obj->rx_task_handle) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RX task!");
            vSemaphoreDelete(obj->rx_mutex);
            obj->initialized = false;
            return -1;
        }
    }

    // 创建发送任务
    if (xTaskCreate(sw_tx_task, "sw_tx_task", 4096, obj, prio, &obj->tx_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX task!");
        vSemaphoreDelete(obj->rx_mutex);
        obj->initialized = false;
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Software serial initialized: TX=%d, RX=%d, baud=%d",
             tx_pin, rx_pin, baudrate);

    return get_instance_id(obj);
}

// 检查是否有数据可读
int serial_available(int instance_id) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return 0;
    sw_serial_t *obj = &serial_instances[instance_id];
    if (!obj->initialized) return 0;

    // 获取互斥锁来安全访问 rx_len
    if (obj->rx_mutex != NULL) {
        if (xSemaphoreTake(obj->rx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            int available = obj->rx_len;
            xSemaphoreGive(obj->rx_mutex);
            return available;
        }
    }
    return 0;
}

// 读取一个字节
uint8_t serial_read(int instance_id) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return 0;
    sw_serial_t *obj = &serial_instances[instance_id];
    if (!obj->initialized) return 0;

    uint8_t data = 0;
    // 获取互斥锁来保护读取操作
    if (obj->rx_mutex != NULL) {
        if (xSemaphoreTake(obj->rx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (obj->rx_len > 0) {
                data = obj->rx_buf[obj->rx_read_idx];
                obj->rx_read_idx = (obj->rx_read_idx + 1) % RX_BUF_SIZE;
                obj->rx_len--;
            }
            xSemaphoreGive(obj->rx_mutex);
        }
    }
    return data;
}

// 查看下一个字节（不删除）
uint8_t serial_peek(int instance_id) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return 0;
    sw_serial_t *obj = &serial_instances[instance_id];
    if (!obj->initialized || obj->rx_len == 0) return 0;

    uint8_t data = 0;
    // 获取互斥锁来保护查看操作
    if (obj->rx_mutex != NULL) {
        if (xSemaphoreTake(obj->rx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (obj->rx_len > 0) {
                data = obj->rx_buf[obj->rx_read_idx];
            }
            xSemaphoreGive(obj->rx_mutex);
        }
    }
    return data;
}

// 发送字符串
void serial_print(int instance_id, char *str) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return;
    sw_serial_t *obj = &serial_instances[instance_id];
    if (!obj->initialized) return;
    
    int len = strlen(str);
    if (len > TX_BUF_SIZE) len = TX_BUF_SIZE;
    
    for (int i = 0; i < len; i++) {
        obj->tx_buf[i] = str[i];
    }
    obj->tx_len = len;
    xTaskNotifyGive(obj->tx_task_handle);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// 发送带换行的字符串
void serial_println(int instance_id, char *str) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return;
    
    char buf[TX_BUF_SIZE];
    int len = strlen(str);
    if (len + 2 > TX_BUF_SIZE) len = TX_BUF_SIZE - 2;
    
    memcpy(buf, str, len);
    buf[len] = '\r';
    buf[len + 1] = '\n';
    
    serial_print(instance_id, buf);
}

// 发送单个字节
void serial_write(int instance_id, uint8_t ch) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return;
    sw_serial_t *obj = &serial_instances[instance_id];
    if (!obj->initialized) return;
    
    obj->tx_buf[0] = ch;
    obj->tx_len = 1;
    xTaskNotifyGive(obj->tx_task_handle);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// 发送缓冲区
void serial_write_buffer(int instance_id, uint8_t *buf, int len) {
    if (instance_id < 0 || instance_id >= MAX_SERIAL_INSTANCES) return;
    sw_serial_t *obj = &serial_instances[instance_id];
    if (!obj->initialized || len > TX_BUF_SIZE) return;
    
    for (int i = 0; i < len; i++) {
        obj->tx_buf[i] = buf[i];
    }
    obj->tx_len = len;
    xTaskNotifyGive(obj->tx_task_handle);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}