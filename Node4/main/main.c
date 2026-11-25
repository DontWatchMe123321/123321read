/**
 * Node 4 - Smart Irrigation Gateway (ESP32-S3)
 * Wi-Fi Station + BLE Mesh Provisioner/GATT Proxy + MQTT gateway.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_station.h"
#include "mesh_bt_init.h"
#include "ble_mesh_provisioner.h"
#include "mqtt_gateway.h"

static const char *TAG = "node4_main";
static const uint16_t NODE3_UNICAST_ADDR = 0x0005;
static const uint16_t NODE1_UNICAST_ADDR = 0x0006;

/* Override weak hook di mqtt_gateway.c:
 * perintah dari MQTT "smartirrigation/node1/cmd" -> BLE Mesh OnOff. */
void mqtt_gateway_on_pump_cmd(const char *data, int len)
{
    if (len < 1) {
        return;
    }
    if (data[0] == '1' || data[0] == '0') {
        uint8_t onoff = (data[0] == '1');
        esp_err_t err = ble_mesh_node_onoff_set(NODE1_UNICAST_ADDR, onoff);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MQTT cmd: node 0x%04x not provisioned (err %d)",
                     NODE1_UNICAST_ADDR, err);
        } else {
            ESP_LOGI(TAG, "MQTT cmd -> node 0x%04x onoff %d",
                     NODE1_UNICAST_ADDR, onoff);
        }
    }
}

static int cmd_mesh_onoff(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: mesh_onoff <0|1> [address_hex]\n");
        return 1;
    }

    if (argv[1][0] != '0' && argv[1][0] != '1') {
        printf("Invalid value. Use 0 (OFF) or 1 (ON).\n");
        return 1;
    }

    uint16_t address = NODE3_UNICAST_ADDR;
    if (argc >= 3) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[2], &end, 16);
        if (!end || *end != '\0' || parsed > 0xFFFF) {
            printf("Invalid address. Example: 0005\n");
            return 1;
        }
        address = (uint16_t)parsed;
    }

    esp_err_t err = ble_mesh_node_onoff_set(address, argv[1][0] == '1');
    if (err != ESP_OK) {
        printf("OnOff failed: 0x%x (node 0x%04x may not be provisioned)\n",
               err, address);
        return 1;
    }

    printf("OnOff request sent to node 0x%04x: %s\n",
           address, argv[1][0] == '1' ? "ON" : "OFF");
    return 0;
}

static int cmd_mesh_config(int argc, char **argv)
{
    char *end = NULL;
    unsigned long parsed;

    if (argc != 2) {
        printf("Usage: mesh_config <address_hex>\n");
        return 1;
    }

    parsed = strtoul(argv[1], &end, 16);
    if (end == argv[1] || !end || *end != '\0' ||
            parsed == 0 || parsed > 0x7FFF) {
        printf("Invalid unicast address. Example: 0007\n");
        return 1;
    }

    esp_err_t err = ble_mesh_node_configure((uint16_t)parsed);
    if (err != ESP_OK) {
        printf("Mesh config failed for 0x%04lx: %s\n",
               parsed, esp_err_to_name(err));
        return 1;
    }

    printf("Configuration started for node 0x%04lx\n", parsed);
    return 0;
}

static void node4_console_task(void *arg)
{
    char line[64];
    size_t line_len = 0;

    ESP_LOGI(TAG,
             "Console ready: mesh_onoff <0|1> [addr] | mesh_config <addr>");
    while (true) {
        int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            if (line_len == 0) {
                continue;
            }
            line[line_len] = '\0';

            char *command = line;
            while (*command && isspace((unsigned char)*command)) {
                command++;
            }
            char *command_end = command + strlen(command);
            while (command_end > command &&
                   isspace((unsigned char)command_end[-1])) {
                *--command_end = '\0';
            }

            char *argv[3] = {command, NULL, NULL};
            char *first = strchr(command, ' ');
            if (first) {
                *first++ = '\0';
                while (*first == ' ') {
                    first++;
                }
                argv[1] = first;
                char *second = strchr(first, ' ');
                if (second) {
                    *second++ = '\0';
                    while (*second == ' ') {
                        second++;
                    }
                    argv[2] = second;
                }
            }

            int argc = argv[2] ? 3 : (argv[1] ? 2 : 1);
            if (strcmp(argv[0], "mesh_config") == 0) {
                cmd_mesh_config(argc, argv);
            } else if (strcmp(argv[0], "mesh_onoff") == 0) {
                cmd_mesh_onoff(argc, argv);
            } else {
                ESP_LOGW(TAG,
                         "Unknown command. Use mesh_onoff or mesh_config");
            }
            line_len = 0;
        } else if (line_len < sizeof(line) - 1) {
            line[line_len++] = (char)ch;
        } else {
            line_len = 0;
            ESP_LOGW(TAG, "Console line too long");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Node 4 gateway starting (stage 2: Wi-Fi + BLE Mesh Provisioner)");

    if (wifi_sta_connect_start() != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi not connected yet; continuing with BLE Mesh");
    }

    /* MQTT gateway: mulai setelah WiFi up (koneksi broker async,
     * log "MQTT connected" muncul saat IP sudah didapat). */
    if (mqtt_gateway_start(CONFIG_MQTT_BROKER_URI) != ESP_OK) {
        ESP_LOGE(TAG, "MQTT gateway failed to start (broker=%s)",
                 CONFIG_MQTT_BROKER_URI);
    }

    /* BLE Mesh: init BT controller + Bluedroid, lalu stack mesh.
     * Wi-Fi dan BLE berbagi antena 2.4 GHz pada ESP32-S3
     * (coexistence ditangani otomatis oleh driver). */
    if (mesh_bt_init() != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth init failed, mesh disabled");
        return;
    }

    if (ble_mesh_provisioner_init() != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh provisioner init failed");
        return;
    }

    xTaskCreate(node4_console_task, "node4_console", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Node 4 ready: Wi-Fi + BLE Mesh Provisioner aktif");
}
