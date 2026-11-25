/**
 * @file board.h
 * @brief Board-level GPIO Node 1: kontrol relay/pompa DC.
 *
 * GPIO & polarity dikonfigurasi via menuconfig
 * (Component config -> Node 1 Pump Configuration):
 *   - CONFIG_NODE1_RELAY_GPIO        (default 23, hindari strapping pins)
 *   - CONFIG_NODE1_RELAY_ACTIVE_LOW  (default y, sesuai modul relay murah)
 *
 * Fail-safe: saat board_init() relay PASTI OFF — pull resistor diarahkan
 * ke level "OFF" sesuai polarity, sehingga boot/power-loss/restart tidak
 * pernah menyalakan pompa. Relay hanya di-write sekali per perubahan state
 * yang sah (tidak di-spin).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Init GPIO relay. Kondisi awal PASTI OFF.
 */
esp_err_t board_init(void);

/**
 * @brief Set relay/pompa. @param on 1 = ON, 0 = OFF.
 */
void board_relay_set(uint8_t on);

/**
 * @return 1 = relay ON, 0 = relay OFF.
 */
uint8_t board_relay_get_state(void);
