/**
 * Protokol pesan vendor BLE Mesh untuk Smart Irrigation.
 * Dipakai bersama oleh:
 *   - Node 2 (sensor, kirim data)
 *   - Node 4 (gateway, terima data -> MQTT)
 *
 * Format payload SENSOR_DATA (5 byte, little-endian):
 *   [0]     soil_pct   : uint8  (0-100)
 *   [1..2]  temp_x10   : int16  (suhu C x 10, mis. 275 = 27.5 C)
 *   [3..4]  humi_x10   : uint16 (humidity % x 10, mis. 600 = 60.0 %)
 */
#pragma once

#include "esp_ble_mesh_defs.h"

/* Company ID Espressif (standar example ESP-IDF) */
#define MESH_VND_CID_ESP            0x02E5

/* Vendor model ID (bebas, asal konsisten antar node) */
#define MESH_VND_MODEL_SRV_ID       0x0000
#define MESH_VND_MODEL_CLI_ID       0x0001

/* Opcode pesan vendor (3-byte opcode: b0 | 0xC00000 | cid) */
#define MESH_VND_OP_SENSOR_DATA     ESP_BLE_MESH_MODEL_OP_3(0xC1, MESH_VND_CID_ESP)

/* Panjang payload sensor data */
#define MESH_SENSOR_PAYLOAD_LEN     5
