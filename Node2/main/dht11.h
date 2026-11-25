#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t dht11_init(gpio_num_t data_gpio);
esp_err_t dht11_read(float *temperature_c, float *humidity_percent);
