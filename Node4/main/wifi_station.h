/**
 * Wi-Fi Station module API.
 */
#pragma once

#include "esp_err.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * Mulai Wi-Fi dalam mode Station.
 * Blokir sampai mendapat IP (timeout 30 detik).
 * @return ESP_OK jika terhubung, ESP_ERR_TIMEOUT jika gagal.
 */
esp_err_t wifi_sta_connect_start(void);

/**
 * Ambil alamat IP saat ini (kosong jika belum terhubung).
 */
esp_err_t wifi_station_get_ip(esp_ip4_addr_t *ip_out);
