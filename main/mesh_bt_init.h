/**
 * @file mesh_bt_init.h
 * @brief Init BT controller + Bluedroid (pola sama dengan Node 4).
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Init & enable BT controller (mode BLE) + Bluedroid stack.
 * Harus dipanggil sebelum esp_ble_mesh_init().
 */
esp_err_t mesh_bt_init(void);
