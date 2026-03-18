#ifndef __MPU6050_H_
#define __MPU6050_H_

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "driver/i2c_master.h"
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "Usuart.h"
#include "serial.h"
#include "mpu6050.h"
// MPU6050 I2C地址
#define MPU6050_ADDRESS_AD0_LOW  0x68  // AD0引脚低电平时的地址
#define MPU6050_ADDRESS_AD0_HIGH 0x69  // AD0引脚高电平时的地址
#define MPU6050_DEFAULT_ADDRESS  MPU6050_ADDRESS_AD0_LOW

// MPU6050寄存器地址
#define MPU6050_REG_SMPLRT_DIV   0x19  // 采样率分频器
#define MPU6050_REG_CONFIG       0x1A  // 配置寄存器
#define MPU6050_REG_GYRO_CONFIG  0x1B  // 陀螺仪配置
#define MPU6050_REG_ACCEL_CONFIG 0x1C  // 加速度计配置
#define MPU6050_REG_WHO_AM_I     0x75  // 设备ID寄存器
#define MPU6050_REG_PWR_MGMT_1   0x6B  // 电源管理寄存器1
#define MPU6050_REG_ACCEL_XOUT_H 0x3B  // 加速度计X轴高字节
#define MPU6050_REG_ACCEL_XOUT_L 0x3C  // 加速度计X轴低字节
#define MPU6050_REG_ACCEL_YOUT_H 0x3D  // 加速度计Y轴高字节
#define MPU6050_REG_ACCEL_YOUT_L 0x3E  // 加速度计Y轴低字节
#define MPU6050_REG_ACCEL_ZOUT_H 0x3F  // 加速度计Z轴高字节
#define MPU6050_REG_ACCEL_ZOUT_L 0x40  // 加速度计Z轴低字节
#define MPU6050_REG_TEMP_OUT_H   0x41  // 温度高字节
#define MPU6050_REG_TEMP_OUT_L   0x42  // 温度低字节
#define MPU6050_REG_GYRO_XOUT_H  0x43  // 陀螺仪X轴高字节
#define MPU6050_REG_GYRO_XOUT_L  0x44  // 陀螺仪X轴低字节
#define MPU6050_REG_GYRO_YOUT_H  0x45  // 陀螺仪Y轴高字节
#define MPU6050_REG_GYRO_YOUT_L  0x46  // 陀螺仪Y轴低字节
#define MPU6050_REG_GYRO_ZOUT_H  0x47  // 陀螺仪Z轴高字节
#define MPU6050_REG_GYRO_ZOUT_L  0x48  // 陀螺仪Z轴低字节

// 设备ID值
#define MPU6050_WHO_AM_I_VALUE   0x68  // MPU6050的ID值

// 加速度计量程配置
enum mpu6050_accel_range {
    MPU6050_ACCEL_RANGE_2_G = 0,   // ±2g
    MPU6050_ACCEL_RANGE_4_G,       // ±4g
    MPU6050_ACCEL_RANGE_8_G,       // ±8g
    MPU6050_ACCEL_RANGE_16_G       // ±16g
};

// 陀螺仪量程配置
enum mpu6050_gyro_range {
    MPU6050_GYRO_RANGE_250_DPS = 0,   // ±250°/s
    MPU6050_GYRO_RANGE_500_DPS,       // ±500°/s
    MPU6050_GYRO_RANGE_1000_DPS,      // ±1000°/s
    MPU6050_GYRO_RANGE_2000_DPS       // ±2000°/s
};

// 传感器数据结构体
typedef struct {
    int16_t ax;    // 加速度计X轴
    int16_t ay;    // 加速度计Y轴
    int16_t az;    // 加速度计Z轴
    int16_t gx;    // 陀螺仪X轴
    int16_t gy;    // 陀螺仪Y轴
    int16_t gz;    // 陀螺仪Z轴
    int16_t temp;  // 温度
} mpu6050_data_t;

// 初始化结构体
typedef struct {
    uint8_t address;                  // I2C地址
    enum mpu6050_accel_range accel_range;  // 加速度计量程
    enum mpu6050_gyro_range gyro_range;    // 陀螺仪量程
    uint16_t sample_rate;             // 采样率(Hz)
} mpu6050_config_t;

// 函数声明

/**
 * @brief 初始化MPU6050传感器
 * @param config 初始化配置指针
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_init(const mpu6050_config_t *config);

/**
 * @brief 获取MPU6050设备ID
 * @param who_am_i 存储设备ID的指针
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_who_am_i(uint8_t *who_am_i);

/**
 * @brief 读取加速度计数据
 * @param data 存储加速度计数据的指针
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_read_accel(mpu6050_data_t *data);

/**
 * @brief 读取陀螺仪数据
 * @param data 存储陀螺仪数据的指针
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_read_gyro(mpu6050_data_t *data);

/**
 * @brief 读取温度和传感器数据
 * @param data 存储所有传感器数据的指针
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_read_all(mpu6050_data_t *data);

/**
 * @brief 读取温度数据
 * @param temp 存储温度值的指针
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_read_temperature(int16_t *temp);

/**
 * @brief 设置加速度计量程
 * @param range 加速度计量程
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_set_accel_range(enum mpu6050_accel_range range);

/**
 * @brief 设置陀螺仪量程
 * @param range 陀螺仪量程
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_set_gyro_range(enum mpu6050_gyro_range range);

/**
 * @brief 设置采样率
 * @param rate 采样率(Hz)
 * @return esp_err_t 错误码
 */
esp_err_t mpu6050_set_sample_rate(uint16_t rate);

/**
 * @brief 解初始化MPU6050
 */
void mpu6050_deinit(void);
void calibrate_gyro(int samples, int delay_ms, int16_t* bias_gx, int16_t* bias_gy, int16_t* bias_gz);
#endif // __MPU6050_H_