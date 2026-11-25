/**
 * @file ble_mesh_node.h
 * @brief BLE Mesh Node 1: composition, Generic OnOff Server, provisioning.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Register semua callback mesh, esp_ble_mesh_init(), dan
 *        enable provisioning bearer PB-ADV (jika belum provisioned).
 * Harus dipanggil setelah mesh_bt_init().
 */
esp_err_t ble_mesh_node_init(void);
