#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t soil_adc_init(void);
esp_err_t soil_adc_read(int *raw_value);
bool soil_adc_percent(int raw_value, int *percentage);
