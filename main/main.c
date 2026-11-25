/**
 * @file main.c
 * @brief Node 1 Smart Irrigation - Pompa DC (Generic OnOff Server)
 *
 * Peran  : BLE Mesh NODE (bukan Provisioner). Diprovision otomatis oleh Node 4.
 * Board  : ESP32 classic DevKit V1.
 * Stack  : Bluedroid + ESP-IDF native BLE Mesh (BT 4.2 feature path).
 * Model  : Configuration Server + Generic OnOff Server (1 element), CID 0x02E5.
 * Relay  : GPIO via Kconfig (default 23), polarity via Kconfig (default
 *          active-LOW, sesuai sketch Arduino lama yang teruji).
 *
 * Logika aplikasi ada di modul:
 *   - board.c        : GPIO relay, fail-safe OFF saat boot
 *   - mesh_bt_init.c : BT controller + Bluedroid
 *   - ble_mesh_node.c: composition, provisioning, Generic OnOff Server
 *   - app_state.c    : persistensi OnOff ke NVS
 *
 * Tidak ada: MQTT, Wi-Fi, sensor, LCD, OTA, deep sleep.
 * Tidak ada loop blink seperti sketch Arduino lama - pompa hanya berubah
 * saat menerima perintah OnOff yang sah dari Node 4.
 */
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "board.h"
#include "mesh_bt_init.h"
#include "ble_mesh_node.h"
#include "app_state.h"

#define TAG "NODE1"

/* Didefinisikan di ble_mesh_node.c (harus diawali DE DE agar
 * provisioner Node 4 tidak mengabaikannya). */
extern uint8_t dev_uuid[16];

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  NODE 1 - SMART IRRIGATION (POMPA DC)");
    ESP_LOGI(TAG, "  ESP32 DevKit | Generic OnOff Server");
    ESP_LOGI(TAG, "========================================");

    /* UUID lengkap (16 byte) */
    ESP_LOG_BUFFER_HEX("NODE1_UUID", dev_uuid, 16);

    /* NVS: dipakai BLE_MESH_SETTINGS (mesh state) + state OnOff aplikasi.
     * erase hanya jika partisi korup - BUKAN erase flash penuh. */
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* FAIL-SAFE: relay PASTI OFF sebelum BT/mesh berjalan. */
    ESP_ERROR_CHECK(board_init());

    err = mesh_bt_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BT init gagal, berhenti (relay tetap OFF)");
        return;
    }

    err = ble_mesh_node_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh init gagal, berhenti (relay tetap OFF)");
        return;
    }

    /* Tidak ada deep sleep: node selalu aktif menerima perintah mesh. */
    ESP_LOGI(TAG, "Node 1 siap. Menunggu provisioning/perintah dari Node 4.");
}
