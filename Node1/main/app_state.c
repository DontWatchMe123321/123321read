/**
 * @file app_state.c
 * @brief Persistensi state OnOff Node 1 ke NVS.
 */
#include "app_state.h"

#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "app_state";

esp_err_t app_state_load_onoff(uint8_t *out_onoff)
{
    if (out_onoff == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_STATE_NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Belum pernah disimpan -> default aman OFF */
        *out_onoff = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open gagal: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t val = 0;
    err = nvs_get_u8(handle, APP_STATE_NVS_KEY, &val);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_onoff = 0;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_u8 gagal: %s", esp_err_to_name(err));
        return err;
    }

    *out_onoff = val ? 1 : 0;
    return ESP_OK;
}

esp_err_t app_state_save_onoff(uint8_t onoff)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_STATE_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open gagal: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, APP_STATE_NVS_KEY, onoff ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gagal simpan onoff ke NVS: %s", esp_err_to_name(err));
    }
    return err;
}
