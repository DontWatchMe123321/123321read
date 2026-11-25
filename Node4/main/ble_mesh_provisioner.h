/**
 * BLE Mesh Provisioner API untuk Node 4.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * Inisialisasi BLE Mesh sebagai Provisioner.
 * Wajib dipanggil SETELAH mesh_bt_init() sukses.
 * Setelah init: provisioner aktif memindai unprovisioned device
 * dengan UUID berawalan 0xDE 0xDE.
 */
esp_err_t ble_mesh_provisioner_init(void);

/**
 * Kirim Generic OnOff Set ke node yang sudah ter-provision.
 * @param unicast alamat unicast node (dari log PROV_COMPLETE)
 * @param onoff 1 = ON, 0 = OFF
 */
esp_err_t ble_mesh_node_onoff_set(uint16_t unicast, uint8_t onoff);

/**
 * Read Composition Data and bind the local AppKey to a supported gateway model.
 * The node must already be provisioned; use this to configure restored nodes.
 */
esp_err_t ble_mesh_node_configure(uint16_t unicast);
