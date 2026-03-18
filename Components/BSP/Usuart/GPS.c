#include "GPS.h"

// 经纬度结构体
typedef struct {
    double latitude;
    double longitude;
    char ns_indicator;  // 'N' or 'S'
    char ew_indicator;  // 'E' or 'W'
    int fix_quality;    // 定位质量 0=无效,1=GPS,2=DGPS
    int satellites;      // 卫星数量
    float altitude;      // 海拔高度
    int hour, minute, second;  // UTC时间
    int day, month, year;      // UTC日期
    bool valid;          // 数据是否有效
} gps_data_t;

static gps_data_t gps_data = {0};

// ============ NMEA句子解析函数 ============

// 解析GGA语句（全球定位系统固定数据）
static void parse_gga(char *nmea_sentence) {
    char *token;
    int field = 0;
    
    token = strtok(nmea_sentence, ",");
    while (token != NULL) {
        switch(field) {
            case 1: // UTC时间
                if (strlen(token) >= 6) {
                    gps_data.hour = (token[0]-'0')*10 + (token[1]-'0');
                    gps_data.minute = (token[2]-'0')*10 + (token[3]-'0');
                    gps_data.second = (token[4]-'0')*10 + (token[5]-'0');
                }
                break;
            case 2: // 纬度
                if (strlen(token) > 0) {
                    double lat = atof(token);
                    int degrees = (int)(lat/100);
                    double minutes = lat - degrees*100;
                    gps_data.latitude = degrees + minutes/60.0;
                }
                break;
            case 3: // 北纬/南纬指示器
                if (strlen(token) > 0) {
                    gps_data.ns_indicator = token[0];
                }
                break;
            case 4: // 经度
                if (strlen(token) > 0) {
                    double lon = atof(token);
                    int degrees = (int)(lon/100);
                    double minutes = lon - degrees*100;
                    gps_data.longitude = degrees + minutes/60.0;
                }
                break;
            case 5: // 东经/西经指示器
                if (strlen(token) > 0) {
                    gps_data.ew_indicator = token[0];
                }
                break;
            case 6: // 定位质量
                if (strlen(token) > 0) {
                    gps_data.fix_quality = atoi(token);
                    gps_data.valid = (gps_data.fix_quality > 0);
                }
                break;
            case 7: // 卫星数量
                if (strlen(token) > 0) {
                    gps_data.satellites = atoi(token);
                }
                break;
            case 9: // 海拔高度
                if (strlen(token) > 0) {
                    gps_data.altitude = atof(token);
                }
                break;
        }
        field++;
        token = strtok(NULL, ",");
    }
}

// 解析RMC语句（推荐的最小导航信息）
static void parse_rmc(char *nmea_sentence) {
    char *token;
    int field = 0;
    
    token = strtok(nmea_sentence, ",");
    while (token != NULL) {
        switch(field) {
            case 1: // UTC时间
                if (strlen(token) >= 6) {
                    gps_data.hour = (token[0]-'0')*10 + (token[1]-'0');
                    gps_data.minute = (token[2]-'0')*10 + (token[3]-'0');
                    gps_data.second = (token[4]-'0')*10 + (token[5]-'0');
                }
                break;
            case 2: // 状态 A=有效, V=无效
                gps_data.valid = (token[0] == 'A');
                break;
            case 3: // 纬度
                if (strlen(token) > 0) {
                    double lat = atof(token);
                    int degrees = (int)(lat/100);
                    double minutes = lat - degrees*100;
                    gps_data.latitude = degrees + minutes/60.0;
                }
                break;
            case 4: // 北纬/南纬
                if (strlen(token) > 0) {
                    gps_data.ns_indicator = token[0];
                }
                break;
            case 5: // 经度
                if (strlen(token) > 0) {
                    double lon = atof(token);
                    int degrees = (int)(lon/100);
                    double minutes = lon - degrees*100;
                    gps_data.longitude = degrees + minutes/60.0;
                }
                break;
            case 6: // 东经/西经
                if (strlen(token) > 0) {
                    gps_data.ew_indicator = token[0];
                }
                break;
            case 9: // UTC日期
                if (strlen(token) >= 6) {
                    gps_data.day = (token[0]-'0')*10 + (token[1]-'0');
                    gps_data.month = (token[2]-'0')*10 + (token[3]-'0');
                    gps_data.year = 2000 + (token[4]-'0')*10 + (token[5]-'0');
                }
                break;
        }
        field++;
        token = strtok(NULL, ",");
    }
}

// 打印GPS数据
static void print_gps_data(void) {
    if (gps_data.valid) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "✅ GPS Fix OK");
        ESP_LOGI(TAG, "经纬度: %.6f%c, %.6f%c", 
                 gps_data.latitude, gps_data.ns_indicator,
                 gps_data.longitude, gps_data.ew_indicator);
        ESP_LOGI(TAG, "海拔: %.1f 米", gps_data.altitude);
        ESP_LOGI(TAG, "卫星数: %d", gps_data.satellites);
        ESP_LOGI(TAG, "定位质量: %d (1=GPS, 2=DGPS)", gps_data.fix_quality);
        ESP_LOGI(TAG, "UTC时间: %02d:%02d:%02d", 
                 gps_data.hour, gps_data.minute, gps_data.second);
        ESP_LOGI(TAG, "UTC日期: %04d-%02d-%02d", 
                 gps_data.year, gps_data.month, gps_data.day);
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGW(TAG, "⏳ 等待GPS定位... 卫星数: %d", gps_data.satellites);
    }
}

// ============ GPS数据接收任务 ============
static void gps_rx_task(void *pvParameters)
{
    uint8_t rx_data[GPS_RX_BUF_SIZE];
    int len;
    char nmea_buffer[GPS_MAX_SENTENCE_LEN];
    int buffer_idx = 0;
    int sentence_count = 0;
    
    
    ESP_LOGI(TAG, "等待GPS模块数据... 请确保天线已接好并放在室外/窗边");
    
    while(1) {
        // 读取串口数据
        len = uart_read_bytes(USART_US, rx_data, sizeof(rx_data), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            // 处理每个接收到的字节
            for (int i = 0; i < len; i++) {
                uint8_t ch = rx_data[i];
                
                // NMEA句子以'$'开头
                if (ch == '$') {
                    buffer_idx = 0;
                    nmea_buffer[buffer_idx++] = ch;
                }
                // 句子结束符（回车或换行）
                else if (ch == '\r' || ch == '\n') {
                    if (buffer_idx > 0) {
                        nmea_buffer[buffer_idx] = '\0';  // 字符串结束
                        
                        // 处理不同类型的NMEA句子
                        if (strstr(nmea_buffer, "$GNGGA") || strstr(nmea_buffer, "$GPGGA")) {
                            // 保存原始数据用于调试
                            ESP_LOGD(TAG, "GGA: %s", nmea_buffer);
                            
                            // 解析GGA语句
                            char temp_buf[GPS_MAX_SENTENCE_LEN];
                            strcpy(temp_buf, nmea_buffer);
                            parse_gga(temp_buf);
                            
                            sentence_count++;
                        }
                        else if (strstr(nmea_buffer, "$GNRMC") || strstr(nmea_buffer, "$GPRMC")) {
                            // 保存原始数据用于调试
                            ESP_LOGD(TAG, "RMC: %s", nmea_buffer);
                            
                            // 解析RMC语句
                            char temp_buf[GPS_MAX_SENTENCE_LEN];
                            strcpy(temp_buf, nmea_buffer);
                            parse_rmc(temp_buf);
                            
                            // 每收到5个句子打印一次位置信息
                            if (sentence_count % 5 == 0) {
                                print_gps_data();
                            }
                        }
                        else {
                            // 其他NMEA句子（GSV, GLL等），只调试打印
                            ESP_LOGD(TAG, "Other NMEA: %s", nmea_buffer);
                        }
                        
                        buffer_idx = 0;  // 重置缓冲区
                    }
                }
                // 正常字符，添加到缓冲区（防止溢出）
                else if (buffer_idx < GPS_MAX_SENTENCE_LEN - 1) {
                    nmea_buffer[buffer_idx++] = ch;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============ 初始化GPS串口 ============
void gps_uart_init(void)
{
    ESP_LOGI(TAG, "Initializing GPS UART...");
    
    // 使用你已有的UART_Init函数
       UART_Init(USART_US,USART_TX_PIN,USART_RX_PIN,115200);
    
  
}

// ============ 启动GPS任务 ============
void start_gps_task(void)
{
    gps_uart_init();
    xTaskCreate(gps_rx_task, "gps_rx", 4096, NULL, 5, NULL);
}