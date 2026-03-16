#ifndef __LED_h_
#define __LED_h_

#include <string.h>
#include <stdio.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// UART0 使用默认引脚
#define    USART_US      UART_NUM_0
#define    USART_TX_PIN  GPIO_NUM_1  // TX
#define    USART_RX_PIN  GPIO_NUM_3  // RX




void LED_Init(uint8_t led_position);
#endif