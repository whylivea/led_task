#include "JGdistance.h"

QueueHandle_t distance_queue = NULL;

// CRC16 Modbus计算函数 (CRC-16/MODBUS X16+X15+X2+1)
uint16_t laser_ranger_crc16(uint8_t *buffer, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    
    for (int i = 0; i < length; i++)
    {
        crc ^= (uint16_t)buffer[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001; // 0xA001是0x8005的反转
            }
            else
            {
                crc = crc >> 1;
            }
        }
    }
    
    return crc;
}

// UART初始化
esp_err_t laser_ranger_init(void)
{
    printf("\n========================================\n");
    printf("TOF400F激光测距模块初始化\n");
    printf("========================================\n");
    
    // 配置UART参数 - 严格按照官方文档：115200, 8, N, 1
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    printf("UART配置: 波特率=115200, 8数据位, 无校验, 1停止位\n");
    printf("TX2(Pin17) -> 模块RX\n");
    printf("RX2(Pin16) <- 模块TX\n");
    
    esp_err_t ret = uart_param_config(LASER_UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK) {
        printf("UART参数配置失败: %d\n", ret);
        return ret;
    }
    
    ret = uart_set_pin(LASER_UART_PORT_NUM, LASER_UART_TXD_PIN, 
                       LASER_UART_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        printf("UART引脚配置失败: %d\n", ret);
        return ret;
    }
    
    ret = uart_driver_install(LASER_UART_PORT_NUM, LASER_UART_BUF_SIZE * 2, 
                               LASER_UART_BUF_SIZE * 2, LASER_UART_QUEUE_SIZE, 
                               NULL, 0);
    if (ret != ESP_OK) {
        printf("UART驱动安装失败: %d\n", ret);
        return ret;
    }
    
    // 清空UART缓冲区
    uart_flush(LASER_UART_PORT_NUM);
    
    // 创建距离数据队列
    distance_queue = xQueueCreate(10, sizeof(uint16_t));
    if (distance_queue == NULL) {
        printf("创建队列失败\n");
        return ESP_ERR_NO_MEM;
    }
    
    printf("UART初始化完成\n");
    printf("========================================\n");
    
    return ESP_OK;
}

// 发送读取距离命令 - 严格按照官方文档示例：01 03 00 10 00 01 85 CF
void laser_ranger_read_distance(void)
{
    uint8_t cmd_buffer[8];
    
    // 构建Modbus读取命令 - 完全按照官方示例
    cmd_buffer[0] = SLAVE_ADDR;           // 从机地址 0x01
    cmd_buffer[1] = MODBUS_CMD_READ;       // 功能码 0x03
    cmd_buffer[2] = 0x00;                   // 寄存器地址高位 0x00
    cmd_buffer[3] = 0x10;                   // 寄存器地址低位 0x10 (0x0010)
    cmd_buffer[4] = 0x00;                   // 读取数量高位 0x00
    cmd_buffer[5] = 0x01;                   // 读取数量低位 0x01 (读1个寄存器)
    
    // 计算CRC (按照官方示例应该是 85 CF)
    uint16_t crc = laser_ranger_crc16(cmd_buffer, 6);
    cmd_buffer[6] = crc & 0xFF;              // CRC低字节
    cmd_buffer[7] = (crc >> 8) & 0xFF;        // CRC高字节
    
    // 发送命令
    uart_write_bytes(LASER_UART_PORT_NUM, (const char *)cmd_buffer, 8);
    uart_wait_tx_done(LASER_UART_PORT_NUM, pdMS_TO_TICKS(10));
    
    printf("发送读命令: ");
    for(int i = 0; i < 8; i++) {
        printf("%02X ", cmd_buffer[i]);
    }
    printf("\n");
}

// 设置测距模式 - 严格按照官方示例：01 06 00 04 00 01 09 CB
void laser_ranger_set_mode(uint16_t mode)
{
    uint8_t cmd_buffer[8];
    
    // 构建Modbus写命令 - 完全按照官方示例
    cmd_buffer[0] = SLAVE_ADDR;           // 从机地址 0x01
    cmd_buffer[1] = MODBUS_CMD_WRITE;      // 功能码 0x06
    cmd_buffer[2] = 0x00;                   // 寄存器地址高位 0x00
    cmd_buffer[3] = 0x04;                   // 寄存器地址低位 0x04 (0x0004)
    cmd_buffer[4] = (mode >> 8) & 0xFF;     // 数据高位
    cmd_buffer[5] = mode & 0xFF;             // 数据低位
    
    // 计算CRC
    uint16_t crc = laser_ranger_crc16(cmd_buffer, 6);
    cmd_buffer[6] = crc & 0xFF;              // CRC低字节
    cmd_buffer[7] = (crc >> 8) & 0xFF;        // CRC高字节
    
    // 发送命令
    uart_write_bytes(LASER_UART_PORT_NUM, (const char *)cmd_buffer, 8);
    uart_wait_tx_done(LASER_UART_PORT_NUM, pdMS_TO_TICKS(10));
    
    printf("设置模式: ");
    for(int i = 0; i < 8; i++) {
        printf("%02X ", cmd_buffer[i]);
    }
    printf(" - %s\n", mode == MODE_HIGH_PRECISION ? "高精度模式(1.3m)" : "长距离模式(4.0m)");
}

// 激光测距主任务
void laser_ranger_task(void *pvParameters)
{
    uint8_t rx_buffer[32];
    uint16_t distance = 0;
    TickType_t last_read_time = xTaskGetTickCount();
    const TickType_t read_interval = pdMS_TO_TICKS(500); // 500ms读取一次
    
    printf("\n激光测距任务启动\n");
    printf("等待模块响应...\n\n");
    
    // 先设置模式为高精度（可选）
    // vTaskDelay(pdMS_TO_TICKS(1000));
    // laser_ranger_set_mode(MODE_HIGH_PRECISION);
    
    while (1) {
        // 每500ms发送一次读取命令
        if ((xTaskGetTickCount() - last_read_time) >= read_interval) {
            laser_ranger_read_distance();
            last_read_time = xTaskGetTickCount();
        }
        
        // 读取UART数据
        int len = uart_read_bytes(LASER_UART_PORT_NUM, rx_buffer, sizeof(rx_buffer), 
                                   pdMS_TO_TICKS(100));
        
        if (len > 0) {
            printf("收到 %d 字节: ", len);
            for(int i = 0; i < len; i++) {
                printf("%02X ", rx_buffer[i]);
            }
            printf("\n");
            
            // 验证是否是有效的Modbus响应
            if (len >= 7) {
                // 计算CRC验证
                uint16_t recv_crc = (rx_buffer[len-1] << 8) | rx_buffer[len-2];
                uint16_t calc_crc = laser_ranger_crc16(rx_buffer, len-2);
                
                if (recv_crc == calc_crc) {
                    // CRC验证通过
                    if (rx_buffer[0] == SLAVE_ADDR && rx_buffer[1] == MODBUS_CMD_READ) {
                        // 读命令响应: 01 03 02 00 65 78 6F
                        if (rx_buffer[2] == 0x02) { // 数据字节个数应为2
                            distance = (rx_buffer[3] << 8) | rx_buffer[4];
                            printf("✓ 距离: %d mm (%.1f cm, %.2f m)\n", 
                                   distance, distance/10.0, distance/1000.0);
                            
                            // 发送到队列
                            if (distance_queue != NULL) {
                                xQueueSend(distance_queue, &distance, 0);
                            }
                        }
                    }
                    else if (rx_buffer[0] == SLAVE_ADDR && rx_buffer[1] == MODBUS_CMD_WRITE) {
                        // 写命令响应: 01 06 00 04 00 01 09 CB
                        printf("✓ 写命令成功\n");
                    }
                } else {
                    printf("✗ CRC验证失败 (计算: %04X, 收到: %04X)\n", calc_crc, recv_crc);
                }
            } else {
                printf("✗ 数据长度太短\n");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}