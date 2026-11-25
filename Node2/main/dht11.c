#include "dht11.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define DHT11_TIMEOUT_US 120

static gpio_num_t s_data_gpio = -1;
static bool s_initialized;

static bool wait_for_level(int level, int64_t timeout_us)
{
    const int64_t start_us = esp_timer_get_time();
    while (gpio_get_level(s_data_gpio) != level) {
        if (esp_timer_get_time() - start_us > timeout_us) {
            return false;
        }
    }
    return true;
}

esp_err_t dht11_init(gpio_num_t data_gpio)
{
    if (!GPIO_IS_VALID_GPIO(data_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << data_gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    s_data_gpio = data_gpio;
    s_initialized = true;
    return gpio_set_level(s_data_gpio, 1);
}

esp_err_t dht11_read(float *temperature_c, float *humidity_percent)
{
    if (!s_initialized ||
        temperature_c == NULL || humidity_percent == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[5] = {0};
    esp_err_t err = gpio_set_direction(s_data_gpio, GPIO_MODE_OUTPUT_OD);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(s_data_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(s_data_gpio, 1);
    esp_rom_delay_us(30);

    err = gpio_set_direction(s_data_gpio, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        return err;
    }

    if (!wait_for_level(0, DHT11_TIMEOUT_US) ||
        !wait_for_level(1, DHT11_TIMEOUT_US) ||
        !wait_for_level(0, DHT11_TIMEOUT_US)) {
        gpio_set_direction(s_data_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_level(s_data_gpio, 1);
        return ESP_ERR_TIMEOUT;
    }

    for (int bit = 0; bit < 40; ++bit) {
        if (!wait_for_level(1, DHT11_TIMEOUT_US)) {
            gpio_set_direction(s_data_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
            gpio_set_level(s_data_gpio, 1);
            return ESP_ERR_TIMEOUT;
        }

        const int64_t high_start_us = esp_timer_get_time();
        if (!wait_for_level(0, DHT11_TIMEOUT_US)) {
            gpio_set_direction(s_data_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
            gpio_set_level(s_data_gpio, 1);
            return ESP_ERR_TIMEOUT;
        }

        data[bit / 8] <<= 1;
        if (esp_timer_get_time() - high_start_us > 40) {
            data[bit / 8] |= 1;
        }
    }

    gpio_set_direction(s_data_gpio, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_data_gpio, 1);

    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    *humidity_percent = (float)data[0] + ((float)data[1] / 10.0f);
    *temperature_c = (float)(data[2] & 0x7f) + ((float)data[3] / 10.0f);
    if ((data[2] & 0x80) != 0) {
        *temperature_c = -*temperature_c;
    }
    return ESP_OK;
}
