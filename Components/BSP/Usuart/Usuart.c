#include "Usuart.h"

void UART_Init(uart_port_t uaurt_num,uint8_t Usuart_tx,uint8_t Usuart_rx,uint64_t baudRate)
{
    
    uart_config_t uart_stuct={
        .baud_rate=baudRate,
        .data_bits=UART_DATA_8_BITS,
        .flow_ctrl= UART_HW_FLOWCTRL_DISABLE ,
        
        .parity= UART_PARITY_DISABLE,
        .rx_flow_ctrl_thresh=100,
        .source_clk=UART_SCLK_DEFAULT,
        .stop_bits=UART_STOP_BITS_1, 

    };
     uart_param_config(uaurt_num, &uart_stuct);
  uart_set_pin(uaurt_num,Usuart_tx, Usuart_rx, -1,-1);
    uart_driver_install(uaurt_num, 1024, 1024, 0, NULL, 0);


}