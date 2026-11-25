/**
 * MQTT Gateway API untuk Node 4.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * Mulai klien MQTT dan hubungkan ke broker.
 * @param broker_uri contoh: "mqtt://192.168.0.50:1883"
 */
esp_err_t mqtt_gateway_start(const char *broker_uri);

/**
 * Publish data sensor dari payload vendor BLE Mesh ke MQTT.
 * Dipanggil dari callback vendor model saat menerima MESH_VND_OP_SENSOR_DATA.
 */
void mqtt_gateway_publish_sensor(uint16_t src_addr, const uint8_t *data, uint16_t len);

/**
 * Publish status pompa (dari Generic OnOff status node lain) ke MQTT.
 */
void mqtt_gateway_publish_pump_status(uint16_t src_addr, uint8_t onoff);

/**
 * Hook perintah pompa dari MQTT (smartirrigation/node1/cmd).
 * Di-override di main.c dengan implementasi nyata (kirim BLE Mesh OnOff).
 */
void mqtt_gateway_on_pump_cmd(const char *data, int len);
