#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "dht11.h"

DHT11_Message dht11_data;
#define TAG "my_task"

esp_err_t DHT11_Init(gpio_num_t dht11_gpio)
{
    gpio_config_t io_config={
        .pin_bit_mask=(1ULL<<dht11_gpio),
        .intr_type=GPIO_INTR_DISABLE,
        .mode=GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en= GPIO_PULLDOWN_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_config));

    //默认设置为高电平
    gpio_set_level(dht11_gpio,1);
    ESP_LOGI(TAG,"dht11 init success,gpio:%d",dht11_gpio);
    return ESP_OK;
}

 esp_err_t dht11_read_data(gpio_num_t dht11_gpio)
{
    gpio_set_direction(dht11_gpio,GPIO_MODE_OUTPUT);
    gpio_set_level(dht11_gpio,0);
    esp_rom_delay_us(18000);//拉低20ms
    gpio_set_level(dht11_gpio,1);
    esp_rom_delay_us(30);//接收模式并拉高20us,释放总线
    gpio_set_direction(dht11_gpio,GPIO_MODE_INPUT);
    //等待dht11响应(应该是83us低电平，87us高电平)
    int timeout=100;
    while (gpio_get_level(dht11_gpio)==1)
    {
        if(timeout--<=0)
        {
                ESP_LOGI(TAG,"dht11,wait low level defeat");
                return ESP_ERR_TIMEOUT;
        }
         esp_rom_delay_us(1);
    }
    timeout=100;
    //等待拉低(87us)
    while(gpio_get_level(dht11_gpio)==0)
    {
        if(timeout--<=0)
        {
                ESP_LOGI(TAG,"dht11 wait high level defeat");
                return ESP_ERR_TIMEOUT;
        }
         esp_rom_delay_us(1);
    }
    timeout=100;
    while(gpio_get_level(dht11_gpio)==1)
    {
        if(timeout--<=0)
        {
              ESP_LOGI(TAG,"dht11 realse high level defeat");
                return ESP_ERR_TIMEOUT;
        }
        esp_rom_delay_us(1);
    }
    
    int data_array[5]={0};
    for(uint8_t i=0;i<5;i++)
    {
        for(uint8_t j=0;j<8;j++)
        {
            //读取数据中，dht信号规定：
            /*1:54us 低电平+23~27us 高电平 代表：0*/
            /*2:54us 低电平+68~74us 高电平 代表：1*/
            //先等待拉高
            timeout=100;
            while(gpio_get_level(dht11_gpio)==0)
            {
                if(timeout--<=0)
                {
                    ESP_LOGI(TAG,"read data wait low level(0)defeat");
                    return ESP_ERR_TIMEOUT;
                }
                esp_rom_delay_us(1);
            }
            timeout=100;
            //那么此时高电平
            esp_rom_delay_us(40);
            if(gpio_get_level(dht11_gpio)==1)//说明是高电平，因为40us>27us了
            {
                data_array[i]|=(1<<(7-j));
                //等待下降沿，即下一帧数据
                while(gpio_get_level(dht11_gpio)==1)
                {
                    if(timeout--<=0)
                    {
                        ESP_LOGI(TAG,"next data wait low defeat");
                        return ESP_ERR_TIMEOUT;
                    }
                    esp_rom_delay_us(1);
                }

            }
              timeout = 100;
            while(gpio_get_level(dht11_gpio) == 0) {
                if(timeout-- <= 0) break;
                esp_rom_delay_us(1);
        }
    }
    }
    //校验和
    if(data_array[0]+data_array[1]+data_array[2]+data_array[3]!=data_array[4])
    {
      ESP_LOGI(TAG,"check data is:%d%d%d%d=%d,but the checksum is:%d",
    data_array[0],data_array[1],data_array[2],data_array[3],
    data_array[0]+data_array[1]+data_array[2]+data_array[3],
    data_array[4]);
    dht11_data.valid=false;
    return ESP_ERR_INVALID_CRC;
    }
    dht11_data.humidity_int=data_array[0];
    dht11_data.humidity_dec=data_array[1];
    dht11_data.temperature_int=data_array[2];
    dht11_data.temperature_dec=data_array[3];
    dht11_data.valid=true;
        ESP_LOGI(TAG,"temperature:%d.%dC,humidity:%d.%d%%",dht11_data.temperature_int,
        dht11_data.temperature_dec,dht11_data.humidity_int,dht11_data.humidity_dec);
    return ESP_OK;

}