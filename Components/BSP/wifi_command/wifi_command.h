#ifndef __wifi_command_h_
#define __wifi_command_h_


typedef enum{
    wifi_connect_disable,  
    wifi_connect_able
}wifi_state;
/*回调函数类型的定义:*/
/*1:p_wifi_state_cb表示函数名称*/
/*2:*p_wifi_state_cb是一个函数指针,用来指向函数地址*/
/*3:参数类型是枚举的wifi_state */
/*4：typedef相当于定义了一个函数指针类型，后面需要定义函数指针变量，也就是.c文件中的WIFI_callback*/
typedef void(*p_wifi_state_cb)(wifi_state);
/*wifi初始化命令*/
/*1:传入回调函数 f*/
void wifi_command_Init(p_wifi_state_cb f);

/*wifi连接函数*/
/*传入字符串id和密码*/
void  wifi_command_connect(const char *ssid,const char *password);
void wifi_state_handler(wifi_state state);
#endif