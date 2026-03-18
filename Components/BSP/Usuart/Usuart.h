#ifndef __Usuart_h_
#define __Usuart_h_

#include <string.h>
#include <stdio.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// UART0 使用默认引脚
#define    USART_US      UART_NUM_2
#define    USART_TX_PIN  GPIO_NUM_17  // TX
#define    USART_RX_PIN  GPIO_NUM_16  // RX



void UART_Init(uart_port_t uaurt_num,uint8_t Usuart_tx,uint8_t Usuart_rx,uint64_t baudRate);

#endif