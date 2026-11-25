/**
 * @file board.c
 * @brief Implementasi GPIO relay/pompa Node 1.
 *
 * Wiring (sesuai briefing):
 *   - Relay IN   -> GPIO23 (default Kconfig)
 *   - Relay VCC  -> 5V (VIN) devkit, Relay GND -> GND devkit
 *   - Pompa      -> supply EKSTERNAL terpisah (bukan 5V ESP32)
 *   - Seri: supply(+) -> COM, NO -> pompa(+), pompa(-) -> supply(-)
 *   - Flyback diode 1N4007 paralel terminal pompa (katoda ke +)
 *   - GND supply pompa digabung dengan GND ESP32 (common ground)
 */
#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board";

#define RELAY_GPIO CONFIG_NODE1_RELAY_GPIO

#if CONFIG_NODE1_RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 1
#else
#define RELAY_ACTIVE_LOW 0
#endif

static uint8_t s_relay_on;

esp_err_t board_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        /* Level idle = relay OFF, dijaga pull internal:
         * active-LOW  -> pin harus HIGH saat idle -> pull-up
         * active-HIGH -> pin harus LOW  saat idle -> pull-down
         * Dengan begitu sejak gpio_config() pin sudah di level OFF,
         * tidak ada jendela dimana relay bisa klik saat boot. */
        .pull_up_en = RELAY_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = RELAY_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config GPIO%d gagal: %s", RELAY_GPIO, esp_err_to_name(err));
        return err;
    }

    /* Fail-safe boot: pompa PASTI OFF sebelum BT/mesh aktif. */
    board_relay_set(0);

    ESP_LOGI(TAG, "Relay: GPIO%d, active-%s, state awal OFF",
             RELAY_GPIO, RELAY_ACTIVE_LOW ? "LOW" : "HIGH");
    return ESP_OK;
}

void board_relay_set(uint8_t on)
{
    s_relay_on = on ? 1 : 0;
#if RELAY_ACTIVE_LOW
    int level = s_relay_on ? 0 : 1;
#else
    int level = s_relay_on ? 1 : 0;
#endif
    gpio_set_level(RELAY_GPIO, level);
}

uint8_t board_relay_get_state(void)
{
    return s_relay_on;
}
