#include "mpu6050.h"
#include "driver/i2c.h"  // 旧版驱动头文件
#include "esp_log.h"

#define TAG "MPU6050"

// I2C总线配置
#define I2C_MASTER_SCL_IO    GPIO_NUM_8
#define I2C_MASTER_SDA_IO    GPIO_NUM_1
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   100000      // 降低到100kHz更稳定
#define I2C_MASTER_TIMEOUT_MS 1000

static bool is_initialized = false;
static uint8_t mpu6050_address = MPU6050_DEFAULT_ADDRESS;
static float accel_sensitivity = 16384.0f;
static float gyro_sensitivity = 131.0f;


// 新增：陀螺仪校准函数
// 参数：采样次数，采样间隔(ms)
// 返回值：三个轴的零偏值
void calibrate_gyro(int samples, int delay_ms, int16_t* bias_gx, int16_t* bias_gy, int16_t* bias_gz) {
    int64_t sum_gx = 0, sum_gy = 0, sum_gz = 0;
    mpu6050_data_t calib_data;

    ESP_LOGI(TAG, "开始陀螺仪校准，请保持模块绝对静止...");

    for (int i = 0; i < samples; i++) {
        if (mpu6050_read_all(&calib_data) == ESP_OK) {
            sum_gx += calib_data.gx;
            sum_gy += calib_data.gy;
            sum_gz += calib_data.gz;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    *bias_gx = (int16_t)(sum_gx / samples);
    *bias_gy = (int16_t)(sum_gy / samples);
    *bias_gz = (int16_t)(sum_gz / samples);

    ESP_LOGI(TAG, "校准完成. 零偏值: X=%d, Y=%d, Z=%d", *bias_gx, *bias_gy, *bias_gz);
}
/**
 * @brief 初始化I2C总线（旧版驱动）
 */
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,  // 使能内部上拉
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C initialized successfully (legacy driver)");
    return ESP_OK;
}

/**
 * @brief 去初始化I2C总线
 */
static void i2c_master_deinit(void)
{
    i2c_driver_delete(I2C_MASTER_NUM);
    ESP_LOGI(TAG, "I2C deinitialized");
}

/**
 * @brief 从MPU6050读取多个寄存器
 */
static esp_err_t mpu6050_read_registers(uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = i2c_master_write_read_device(
        I2C_MASTER_NUM,
        mpu6050_address,
        &reg_addr, 1,
        data, len,
        pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read registers 0x%02X: %s", reg_addr, esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 向MPU6050写入单个寄存器
 */
static esp_err_t mpu6050_write_register(uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    
    esp_err_t ret = i2c_master_write_to_device(
        I2C_MASTER_NUM,
        mpu6050_address,
        write_buf, 2,
        pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS)
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02X: %s", reg_addr, esp_err_to_name(ret));
    }

    return ret;
}

// 下面的函数基本保持不变，只需要修改变量引用方式
// 删除了 i2c_bus_handle 和 i2c_dev_handle 相关代码

esp_err_t mpu6050_init(const mpu6050_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (is_initialized) {
        ESP_LOGW(TAG, "MPU6050 already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    // 保存地址
    mpu6050_address = config->address;

    // 初始化I2C
    ret = i2c_master_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // 尝试两个可能的地址
    uint8_t addresses_to_try[2] = {mpu6050_address, (mpu6050_address == 0x68) ? 0x69 : 0x68};
    bool device_found = false;

    for (int i = 0; i < 2; i++) {
        mpu6050_address = addresses_to_try[i];
        
        // 验证设备ID
        uint8_t who_am_i;
        ret = mpu6050_read_registers(MPU6050_REG_WHO_AM_I, &who_am_i, 1);
        
        if (ret == ESP_OK && who_am_i == MPU6050_WHO_AM_I_VALUE) {
            device_found = true;
            ESP_LOGI(TAG, "MPU6050 found at address 0x%02X", mpu6050_address);
            break;
        }
    }

    if (!device_found) {
        ESP_LOGE(TAG, "MPU6050 not found at any address");
        i2c_master_deinit();
        return ESP_ERR_NOT_FOUND;
    }

    // 复位设备
    ret = mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x80);
    if (ret != ESP_OK) goto error;
    vTaskDelay(pdMS_TO_TICKS(100));

    // 唤醒设备
    ret = mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret != ESP_OK) goto error;
    vTaskDelay(pdMS_TO_TICKS(20));

    // 设置采样率
    ret = mpu6050_set_sample_rate(config->sample_rate);
    if (ret != ESP_OK) goto error;

    // 设置加速度计量程
    ret = mpu6050_set_accel_range(config->accel_range);
    if (ret != ESP_OK) goto error;

    // 设置陀螺仪量程
    ret = mpu6050_set_gyro_range(config->gyro_range);
    if (ret != ESP_OK) goto error;

    // 配置低通滤波器
    ret = mpu6050_write_register(MPU6050_REG_CONFIG, 0x00);
    if (ret != ESP_OK) goto error;

    is_initialized = true;
    ESP_LOGI(TAG, "MPU6050 initialized successfully");

    return ESP_OK;

error:
    i2c_master_deinit();
    return ret;
}



esp_err_t mpu6050_set_sample_rate(uint16_t rate)
{
    if (rate < 4 || rate > 1000) {
        ESP_LOGE(TAG, "Invalid sample rate: %d", rate);
        return ESP_ERR_INVALID_ARG;
    }

    // MPU6050采样率计算公式: Sample Rate = 1000 / (1 + SMPLRT_DIV)
    // 这里我们使用内部8kHz采样率，通过分频得到所需采样率
    uint8_t div = (8000 / rate) - 1;
    
    esp_err_t ret = mpu6050_write_register(MPU6050_REG_SMPLRT_DIV, div);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sample rate set to %d Hz", rate);
    }
    return ret;
}

esp_err_t mpu6050_set_accel_range(enum mpu6050_accel_range range)
{
    uint8_t config;
    esp_err_t ret = mpu6050_read_registers(MPU6050_REG_ACCEL_CONFIG, &config, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    config &= ~0x18;  // 清除AFS_SEL位 [4:3]
    config |= (range << 3) & 0x18;

    ret = mpu6050_write_register(MPU6050_REG_ACCEL_CONFIG, config);
    if (ret != ESP_OK) {
        return ret;
    }

    // 更新灵敏度因子（如果需要可以取消注释）
    // switch (range) {
    //     case MPU6050_ACCEL_RANGE_2_G:
    //         accel_sensitivity = 16384.0f;
    //         break;
    //     case MPU6050_ACCEL_RANGE_4_G:
    //         accel_sensitivity = 8192.0f;
    //         break;
    //     case MPU6050_ACCEL_RANGE_8_G:
    //         accel_sensitivity = 4096.0f;
    //         break;
    //     case MPU6050_ACCEL_RANGE_16_G:
    //         accel_sensitivity = 2048.0f;
    //         break;
    // }

    ESP_LOGI(TAG, "Accel range set to %d", range);
    return ESP_OK;
}

esp_err_t mpu6050_set_gyro_range(enum mpu6050_gyro_range range)
{
    uint8_t config;
    esp_err_t ret = mpu6050_read_registers(MPU6050_REG_GYRO_CONFIG, &config, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    config &= ~0x18;  // 清除FS_SEL位 [4:3]
    config |= (range << 3) & 0x18;

    ret = mpu6050_write_register(MPU6050_REG_GYRO_CONFIG, config);
    if (ret != ESP_OK) {
        return ret;
    }

    // 更新灵敏度因子（如果需要可以取消注释）
    // switch (range) {
    //     case MPU6050_GYRO_RANGE_250_DPS:
    //         gyro_sensitivity = 131.0f;
    //         break;
    //     case MPU6050_GYRO_RANGE_500_DPS:
    //         gyro_sensitivity = 65.5f;
    //         break;
    //     case MPU6050_GYRO_RANGE_1000_DPS:
    //         gyro_sensitivity = 32.8f;
    //         break;
    //     case MPU6050_GYRO_RANGE_2000_DPS:
    //         gyro_sensitivity = 16.4f;
    //         break;
    // }

    ESP_LOGI(TAG, "Gyro range set to %d", range);
    return ESP_OK;
}

esp_err_t mpu6050_read_all(mpu6050_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[14];
    esp_err_t ret = mpu6050_read_registers(MPU6050_REG_ACCEL_XOUT_H, buffer, 14);
    if (ret != ESP_OK) {
        return ret;
    }

    data->ax = (int16_t)((buffer[0] << 8) | buffer[1]);
    data->ay = (int16_t)((buffer[2] << 8) | buffer[3]);
    data->az = (int16_t)((buffer[4] << 8) | buffer[5]);
    data->temp = (int16_t)((buffer[6] << 8) | buffer[7]);
    data->gx = (int16_t)((buffer[8] << 8) | buffer[9]);
    data->gy = (int16_t)((buffer[10] << 8) | buffer[11]);
    data->gz = (int16_t)((buffer[12] << 8) | buffer[13]);

    return ESP_OK;
}