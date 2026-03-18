#ifndef __JGdistance_H
#define __JGdistance_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// UART配置
#define LASER_UART_PORT_NUM      UART_NUM_2
#define LASER_UART_TXD_PIN       GPIO_NUM_17  // TX2引脚 接模块RX
#define LASER_UART_RXD_PIN       GPIO_NUM_16  // RX2引脚 接模块TX
#define LASER_UART_BUF_SIZE      1024
#define LASER_UART_QUEUE_SIZE    20

// 从机地址
#define SLAVE_ADDR                0x01        // 默认从机地址

// 寄存器地址 (根据官方文档)
#define REG_DISTANCE               0x0010      // 测量结果寄存器
#define REG_MODE_SELECT            0x0004      // 测距模式选择寄存器 (高精度/长距离)

// Modbus命令码
#define MODBUS_CMD_READ            0x03
#define MODBUS_CMD_WRITE           0x06

// 测距模式
#define MODE_HIGH_PRECISION        0x0000      // 高精度模式 (30ms, 1.3m)
#define MODE_LONG_DISTANCE         0x0001      // 长距离模式 (200ms, 4.0m)

// 距离数据队列
extern QueueHandle_t distance_queue;

// 函数声明
esp_err_t laser_ranger_init(void);
void laser_ranger_read_distance(void);
void laser_ranger_set_mode(uint16_t mode);
uint16_t laser_ranger_crc16(uint8_t *buffer, uint16_t length);
void laser_ranger_task(void *pvParameters);

#endif