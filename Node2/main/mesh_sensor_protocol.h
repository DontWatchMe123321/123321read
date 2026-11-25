#pragma once

#include "esp_ble_mesh_defs.h"

/* Node 2 and Node 4 share this vendor message and five-byte payload. */
#define MESH_VND_CID_ESP        0x02E5
#define MESH_VND_MODEL_SRV_ID   0x0000
#define MESH_VND_MODEL_CLI_ID   0x0001
#define MESH_VND_OP_SENSOR_DATA ESP_BLE_MESH_MODEL_OP_3(0xC1, MESH_VND_CID_ESP)
#define MESH_SENSOR_PAYLOAD_LEN 5

/* Payload: soil %, temperature x10 (signed LE16), air humidity x10 (LE16). */
