/**
 * Bluetooth init API untuk BLE Mesh.
 */
#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define TAG_BLE_MESH "ble_mesh"

/**
 * Init & enable Bluetooth controller + Bluedroid host.
 * Panggil SEBELUM ble_mesh_provisioner_init().
 */
esp_err_t mesh_bt_init(void);

/**
 * Isi 16-byte device UUID: byte0-1 bebas (filter), byte2-7 = BT address.
 */
void mesh_bt_get_dev_uuid(uint8_t *dev_uuid);
