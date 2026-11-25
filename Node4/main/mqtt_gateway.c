/**
 * MQTT Gateway untuk Node 4.
 * Menghubungkan jaringan BLE Mesh ke broker MQTT (Mosquitto di PC).
 *
 * Topik:
 *   smartirrigation/node2/soil   <- data kelembapan tanah (persen)
 *   smartirrigation/node2/env    <- suhu & humidity DHT11 (JSON)
 *   smartirrigation/node1/pump   <- status pompa ON/OFF (dari OnOff status)
 *   smartirrigation/node1/cmd    <- subscribe: "1"/"0" untuk kendali pompa
 *   smartirrigation/gateway/status <- LWT/online status gateway
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "mesh_sensor_protocol.h"
#include "mqtt_gateway.h"

static const char *TAG = "mqtt_gateway";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

/* ---------- callback MQTT ---------- */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected to broker");
        esp_mqtt_client_subscribe(s_client, "smartirrigation/node1/cmd", 1);
        esp_mqtt_client_publish(s_client, "smartirrigation/gateway/status",
                                "online", 0, 1, 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        /* Perintah dari dashboard: smartirrigation/node1/cmd "1"/"0" */
        ESP_LOGI(TAG, "MQTT cmd: topic=%.*s data=%.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        mqtt_gateway_on_pump_cmd(event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

/* ---------- API publik ---------- */

esp_err_t mqtt_gateway_start(const char *broker_uri)
{
    if (!broker_uri || !*broker_uri) {
        ESP_LOGE(TAG, "Broker URI kosong, MQTT tidak dimulai");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_uri,
        .session.keepalive = 30,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client start failed: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "MQTT gateway starting, broker=%s", broker_uri);
    return ESP_OK;
}

void mqtt_gateway_publish_sensor(uint16_t src_addr, const uint8_t *data, uint16_t len)
{
    if (!s_connected || !data || len < MESH_SENSOR_PAYLOAD_LEN) {
        return;
    }

    uint8_t soil = data[0];
    int16_t temp_x10 = (int16_t)(data[1] | (data[2] << 8));
    uint16_t humi_x10 = (uint16_t)(data[3] | (data[4] << 8));

    char topic[48];
    char payload[96];

    snprintf(topic, sizeof(topic), "smartirrigation/node%04x/soil", src_addr);
    snprintf(payload, sizeof(payload), "%u", soil);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "smartirrigation/node%04x/env", src_addr);
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.1f,\"humidity\":%.1f}",
             temp_x10 / 10.0f, humi_x10 / 10.0f);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);

    ESP_LOGI(TAG, "Published sensor data from 0x%04x: soil=%u%% temp=%.1fC hum=%.1f%%",
             src_addr, soil, temp_x10 / 10.0f, humi_x10 / 10.0f);
}

void mqtt_gateway_publish_pump_status(uint16_t src_addr, uint8_t onoff)
{
    if (!s_connected) {
        return;
    }
    char topic[48];
    snprintf(topic, sizeof(topic), "smartirrigation/node%04x/pump", src_addr);
    esp_mqtt_client_publish(s_client, topic, onoff ? "ON" : "OFF", 0, 1, 1);
    ESP_LOGI(TAG, "Published pump status 0x%04x: %s",
             src_addr, onoff ? "ON" : "OFF");
}

__attribute__((weak)) void mqtt_gateway_on_pump_cmd(const char *data, int len)
{
    /* Di-override oleh main.c: terjemahkan "1"/"0" menjadi
     * ble_mesh_node_onoff_set(NODE1_ADDR, val). Weak default: no-op. */
    (void)data;
    (void)len;
}
