
idf_component_register(
    SRCS "main.c" "dht11.c" "soil_adc.c"
    INCLUDE_DIRS "."
    REQUIRES bt esp_adc esp_driver_gpio esp_timer esp_rom nvs_flash freertos
)
