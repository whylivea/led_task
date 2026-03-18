#include "onenet_dm.h"
#include "driver\ledc.h"
#include "string.h"
#include "dht11.h"

static int led_brightness=0;
static int led_status=0;

static int humidity_flag=0;
static int temp_flag=0;

static float current_temperature = 0.0;
static int64_t current_humidity = 0;

void onenet_dm_Init(void)
{
    /*led定时器初始化*/
    ledc_timer_config_t led_config={
        .clk_cfg=LEDC_AUTO_CLK,
        .freq_hz=4000,
        .duty_resolution=LEDC_TIMER_12_BIT,
        .timer_num=LEDC_TIMER_0
    };    
    ledc_timer_config(&led_config);
    /*PWM通道初始化*/
    ledc_channel_config_t led_channel=
    {
        .channel=LEDC_CHANNEL_0,
        .duty=0,
        .gpio_num=GPIO_NUM_2,
        .timer_sel=LEDC_TIMER_0
    };
    ledc_channel_config(&led_channel);
    ledc_fade_func_install(0);
}

    /*
    {
  "id": "123",
  "version": "1.0",
  "params": {
    "Brightness":"50"
    "LightSwitch":"True"
    "RGBColor":{
        "Red":100,
        "Green":100,
        "Blue":100,
    }
    }
}
    */
/*功能效果的修改主要在这里*/
/*后续在这里进行代码的修改*/
void onenet_property_handle(cJSON* property)
{
    cJSON *param_js = cJSON_GetObjectItem(property, "params");
    
    if (param_js)
    {
        cJSON *name_js = param_js->child;
        while (name_js)
        {
            if (strcmp(name_js->string, "humi_value") == 0)
            {
                // 注意：平台下发的是直接数值 {"Brightness": 50}
                 humidity_flag= cJSON_GetNumberValue(name_js);
                ESP_LOGI("onenet_dm", "Received target humidity: %lld", humidity_flag);
            }
            else if (strcmp(name_js->string, "temp_value") == 0)
            {
               temp_flag=cJSON_GetNumberValue(name_js);
               ESP_LOGI("onenet_dm", "Received target temperature: %.1f", temp_flag);
            }
            
            name_js = name_js->next;  
        }
    }
}


/*还有这里的上传功能也需要修改*/
cJSON *onenet_property_upload(void)
{
    /*
    {
  "id": "123",
  "version": "1.0",
  "params": {
    "Temperature":{"value":25.5},
    "Humidity":{"value":60，因为onenet平台中湿度的代码是int64_t的，所以上面的代码我要强转}
    }
}
    */
    cJSON*root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"id","123");
    cJSON_AddStringToObject(root,"version","1.0");
    cJSON*parm_js=cJSON_AddObjectToObject(root,"params");
    //亮度
   /* cJSON*brightness_js=cJSON_AddObjectToObject(parm_js,"Brightness");
    cJSON_AddNumberToObject(brightness_js,"value",led_brightness);
    //开关
    cJSON*switch_js=cJSON_AddObjectToObject(parm_js,"LightSwitch");
    cJSON_AddBoolToObject(switch_js,"value",led_status);*/
    //温度
    cJSON*temperature_js=cJSON_AddObjectToObject(parm_js,"temp_value");
    int temperature = dht11_data.temperature_int + dht11_data.temperature_dec * 0.1;
    cJSON_AddNumberToObject(temperature_js,"value",temperature);
    //湿度
    cJSON*humidity_js=cJSON_AddObjectToObject(parm_js,"humi_value");
    int64_t humidity = (int64_t)(dht11_data.humidity_int + dht11_data.humidity_dec * 0.1);
    cJSON_AddNumberToObject(humidity_js,"value",humidity);
    return root;
}