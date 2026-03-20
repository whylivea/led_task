#include "JGdistance.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "VL53L0";

#define VL53L0_UART_PORT UART_NUM_1

// 距离阈值(mm)
#define DISTANCE_0_3M  300  // 0.3m -> 发送'a'
#define DISTANCE_0_6M  600  // 0.6m -> 发送'b'
#define DISTANCE_0_9M  900  // 0.9m -> 发送'c'
#define DISTANCE_1_2M  1200 // 1.2m -> 发送'd'

#define SEND_DELAY_MS  5000

typedef struct {
    bool sent_0_3m;
    bool sent_0_6m;
    bool sent_0_9m;
    bool sent_1_2m;
    int last_distance;
} threshold_state_t;

static threshold_state_t state = {
    .sent_0_3m = false,
    .sent_0_6m = false,
    .sent_0_9m = false,
    .sent_1_2m = false,
    .last_distance = 0
};

extern void send_to_voice(char c);

void reset_threshold_states(void) {
    memset(&state, 0, sizeof(state));
    ESP_LOGI(TAG, "States reset");
}

static void check_and_send(int distance) {
    if (distance <= 0) return;
    
    ESP_LOGI(TAG, "Checking distance: %d mm", distance);  // 添加调试
    
    // 0.3m
    if (distance >= DISTANCE_0_3M && distance < DISTANCE_0_6M) {
        if (!state.sent_0_3m) {
            ESP_LOGI(TAG, "0.3m reached (%d mm), sending 'a'", distance);
            send_to_voice('a');
            state.sent_0_3m = true;
            vTaskDelay(pdMS_TO_TICKS(SEND_DELAY_MS));
        }
    } else {
        state.sent_0_3m = false;
    }
    
    // 0.6m
    if (distance >= DISTANCE_0_6M && distance < DISTANCE_0_9M) {
        if (!state.sent_0_6m) {
            ESP_LOGI(TAG, "0.6m reached (%d mm), sending 'b'", distance);
            send_to_voice('b');
            state.sent_0_6m = true;
            vTaskDelay(pdMS_TO_TICKS(SEND_DELAY_MS));
        }
    } else {
        state.sent_0_6m = false;
    }
    
    // 0.9m
    if (distance >= DISTANCE_0_9M && distance < DISTANCE_1_2M) {
        if (!state.sent_0_9m) {
            ESP_LOGI(TAG, "0.9m reached (%d mm), sending 'c'", distance);
            send_to_voice('c');
            state.sent_0_9m = true;
            vTaskDelay(pdMS_TO_TICKS(SEND_DELAY_MS));
        }
    } else {
        state.sent_0_9m = false;
    }
    
    // 1.2m
    if (distance >= DISTANCE_1_2M) {
        if (!state.sent_1_2m) {
            ESP_LOGI(TAG, "1.2m reached (%d mm), sending 'd'", distance);
            send_to_voice('d');
            state.sent_1_2m = true;
            vTaskDelay(pdMS_TO_TICKS(SEND_DELAY_MS));
        }
    } else {
        state.sent_1_2m = false;
    }
    
    state.last_distance = distance;
}

// 修改后的解析函数 - 适配您的实际数据格式
static int parse_distance(const char *line) {
    // 跳过空行
    if (strlen(line) == 0) return 0;
    
    // 打印原始行用于调试
    ESP_LOGD(TAG, "Raw line: '%s'", line);
    
    // 查找距离行 (包含 "d:")
    const char *d_ptr = strstr(line, "d:");
    if (d_ptr != NULL) {
        // 跳过 "d:" 和空格
        d_ptr += 2;
        while (*d_ptr == ' ') d_ptr++;
        
        // 读取数字
        int distance = atoi(d_ptr);
        if (distance > 0) {
            ESP_LOGI(TAG, "Found distance: %d mm", distance);
            return distance;
        }
    }
    
    return 0;
}

void vl53l0_task(void *pvParameters) {
    ESP_LOGI(TAG, "VL53L0 task started");
    
    char line[64];
    int idx = 0;
    int line_count = 0;
    
    uart_flush(VL53L0_UART_PORT);
    reset_threshold_states();
    
    while (1) {
        size_t available;
        uart_get_buffered_data_len(VL53L0_UART_PORT, &available);
        
        if (available > 0) {
            uint8_t data;
            if (uart_read_bytes(VL53L0_UART_PORT, &data, 1, 0) == 1) {
                char c = (char)data;
                printf("%c", c);  // 调试输出
                
                if (c != '\r' && c != '\n' && idx < sizeof(line)-1) {
                    line[idx++] = c;
                }
                
                if (c == '\n') {
                    line[idx] = '\0';
                    
                    // 去除行尾的回车
                    int len = strlen(line);
                    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) {
                        line[len-1] = '\0';
                        len--;
                    }
                    
                    if (len > 0) {
                        line_count++;
                        int distance = parse_distance(line);
                        
                        if (distance > 0) {
                            ESP_LOGI(TAG, "[Line %d] Distance: %d mm", line_count, distance);
                            check_and_send(distance);
                        }
                    }
                    
                    idx = 0;
                    memset(line, 0, sizeof(line));
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}