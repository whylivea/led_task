// serial.h
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
// 函数声明
int serial_begin(int baudrate, int rx_pin, int tx_pin);
int serial_available(int instance_id);
uint8_t serial_read(int instance_id);
uint8_t serial_peek(int instance_id);
void serial_print(int instance_id, char *str);
void serial_println(int instance_id, char *str);
void serial_write(int instance_id, uint8_t ch);
void serial_write_buffer(int instance_id, uint8_t *buf, int len);

#endif