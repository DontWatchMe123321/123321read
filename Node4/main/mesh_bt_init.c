/**
 * Inisialisasi Bluetooth controller + host untuk BLE Mesh (Bluedroid).
 * Diadaptasi dari contoh resmi ESP-IDF
 * examples/bluetooth/esp_ble_mesh/common_components/example_init.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

#include "mesh_bt_init.h"
#include "esp_ble_mesh_defs.h"

void mesh_bt_get_dev_uuid(uint8_t *dev_uuid)
{
    if (dev_uuid == NULL) {
        return;
    }
    /* Byte 0..1 dipakai untuk filter match provisioner (0xDE 0xDE),
     * byte 2..7 = alamat Bluetooth board ini (unik per board). */
    memcpy(dev_uuid + 2, esp_bt_dev_get_address(), BD_ADDR_LEN);
}

esp_err_t mesh_bt_init(void)
{
    esp_err_t ret;

    /* Classic BT tidak dipakai, release memorinya (hemat ~30KB) */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG_BLE_MESH, "bt controller init failed %d", ret);
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG_BLE_MESH, "bt controller enable failed %d", ret);
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG_BLE_MESH, "bluedroid init failed %d", ret);
        return ret;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG_BLE_MESH, "bluedroid enable failed %d", ret);
        return ret;
    }

    return ESP_OK;
}
