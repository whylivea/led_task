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

#define SEND_DELAY_MS  1000

typedef struct {
    char last_char_sent;  // Track last character sent (0 = none)
    int last_distance;
} threshold_state_t;

static threshold_state_t state = {
    .last_char_sent = 0,
    .last_distance = 0
};

extern void send_to_voice(char c);

void reset_threshold_states(void) {
    memset(&state, 0, sizeof(state));
    ESP_LOGI(TAG, "States reset");
}

static void check_and_send(int distance) {
    if (distance <= 0) return;

    ESP_LOGI(TAG, "Checking distance: %d mm", distance);

    // Determine which range the distance is in
    char char_to_send = 0;
    bool in_range = false;

    // Use strict boundaries to avoid overlap
    if (distance < DISTANCE_0_3M) {
        // Too close, no character to send
        in_range = false;
    } else if (distance < DISTANCE_0_6M) {  // 300-600mm
        char_to_send = 'a';
        in_range = true;
    } else if (distance < DISTANCE_0_9M) {  // 600-900mm
        char_to_send = 'b';
        in_range = true;
    } else if (distance < DISTANCE_1_2M) {  // 900-1200mm
        char_to_send = 'c';
        in_range = true;
    } else {  // 1200mm+
        char_to_send = 'd';
        in_range = true;
    }

     // 获取当前时间
    static TickType_t last_send_time = 0;
    TickType_t now = xTaskGetTickCount();
    
     // 方案1：每次发送后立即重置，下次同一范围还会再发（需要时间间隔控制）
    if (in_range) {
        // 检查是否超过发送间隔
        if ((now - last_send_time) >= pdMS_TO_TICKS(SEND_DELAY_MS)) {
            ESP_LOGI(TAG, "Distance %d mm, sending '%c'", distance, char_to_send);
            send_to_voice(char_to_send);
            last_send_time = now;
            // 不记录 last_char_sent，或者发送后立即重置
            state.last_char_sent = 0;  // 重置，下次还会发
        }
    } else {
        // 离开范围时重置时间
        last_send_time = 0;
        state.last_char_sent = 0;
    }

    

    state.last_distance = distance;
}

static int parse_distance(const char *line) {
    // 跳过空行
    if (strlen(line) == 0) return 0;
    
    // 打印原始行用于调试（可选，正式使用时可以注释掉）
    // ESP_LOGD(TAG, "Raw line: '%s'", line);
    
    // 过滤掉状态行（包含 "State" 或 "Range Valid" 的行）
    if (strstr(line, "State") != NULL || strstr(line, "Range Valid") != NULL) {
        // 这是状态行，不处理
        return 0;
    }
    
    // 过滤掉包含 "Phase Fail" 的错误行
    if (strstr(line, "Phase Fail") != NULL) {
        ESP_LOGW(TAG, "Skipping Phase Fail line");
        return 0;
    }
    
    // 查找距离行 (包含 "d:")
    const char *d_ptr = strstr(line, "d:");
    if (d_ptr != NULL) {
        // 跳过 "d:" 和空格
        d_ptr += 2;
        while (*d_ptr == ' ') d_ptr++;
        
        // 读取数字
        int distance = atoi(d_ptr);
        
        // 过滤无效值（如8190mm的错误值）
        if (distance > 0 && distance < 2000) {  // 假设有效范围0-2000mm
            ESP_LOGI(TAG, "Found distance: %d mm", distance);
            return distance;
        } else if (distance >= 2000) {
            ESP_LOGW(TAG, "Invalid distance: %d mm (likely error code)", distance);
            return 0;
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