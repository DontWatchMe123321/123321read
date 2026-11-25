/**
 * @file app_state.h
 * @brief Persistensi state OnOff Node 1 ke NVS.
 *
 * BLE Mesh settings (CONFIG_BLE_MESH_SETTINGS) menyimpan netkey/appkey/
 * unicast address secara otomatis. State OnOff aplikasi disimpan terpisah
 * di NVS agar status tidak berubah setelah reboot tanpa perintah baru.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#define APP_STATE_NVS_NS   "node1"
#define APP_STATE_NVS_KEY  "onoff"

/**
 * @brief Baca state OnOff tersimpan dari NVS.
 * @param out_onoff Pointer ke uint8_t, diisi 0/1. Default 0 (OFF) jika
 *                  belum pernah tersimpan.
 * @return ESP_OK jika sukses atau key belum ada (default OFF),
 *         error lain bila NVS bermasalah.
 */
esp_err_t app_state_load_onoff(uint8_t *out_onoff);

/**
 * @brief Simpan state OnOff ke NVS (commit langsung).
 */
esp_err_t app_state_save_onoff(uint8_t onoff);
