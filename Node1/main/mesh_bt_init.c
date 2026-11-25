/**
 * @file mesh_bt_init.c
 * @brief BT controller + Bluedroid init untuk BLE Mesh Node.
 *
 * Bluedroid (bukan NimBLE), sesuai arah teknologi project.
 * ESP32 classic: controller di-enable mode BLE saja — mesh tidak butuh BR/EDR.
 */
#include "mesh_bt_init.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"

static const char *TAG = "mesh_bt";

esp_err_t mesh_bt_init(void)
{
    esp_err_t err;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "controller init gagal: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "controller enable gagal: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init gagal: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid enable gagal: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "BT controller + Bluedroid siap");
    return ESP_OK;
}
