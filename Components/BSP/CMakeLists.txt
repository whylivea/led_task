set(src_dirs
    wifi_command
    Usuart
    oled
    LED)
    
set(include_dirs
    wifi_command
    Usuart
    oled
    LED)
    
set(require
    esp_wifi      # WiFi驱动
    esp_event     # 事件处理
    esp_netif     # 网络接口
    nvs_flash
    mqtt
    driver
    bt)

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${require}
)

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)