#include "soil_adc.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define TAG "SOIL_ADC"
#define SOIL_ADC_CHANNEL ADC_CHANNEL_6

static adc_oneshot_unit_handle_t s_adc_handle;

esp_err_t soil_adc_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(
        s_adc_handle, SOIL_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel configuration failed: %s",
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t soil_adc_read(int *raw_value)
{
    if (raw_value == NULL || s_adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return adc_oneshot_read(s_adc_handle, SOIL_ADC_CHANNEL, raw_value);
}

bool soil_adc_percent(int raw_value, int *percentage)
{
    if (percentage == NULL) {
        return false;
    }

#if defined(CONFIG_NODE2_SOIL_ADC_DRY_RAW) && \
    defined(CONFIG_NODE2_SOIL_ADC_WET_RAW)
    const int dry_raw = CONFIG_NODE2_SOIL_ADC_DRY_RAW;
    const int wet_raw = CONFIG_NODE2_SOIL_ADC_WET_RAW;
    if (dry_raw == wet_raw) {
        return false;
    }

    int value = ((raw_value - dry_raw) * 100) / (wet_raw - dry_raw);
    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }
    *percentage = value;
    return true;
#else
    (void)raw_value;
    return false;
#endif
}
