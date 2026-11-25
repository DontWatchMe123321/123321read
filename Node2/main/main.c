#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sleep.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_low_power_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "dht11.h"
#include "soil_adc.h"
#include "mesh_sensor_protocol.h"

#define TAG "NODE_2"
#define GATEWAY_UNICAST_ADDR 0x0001
#define DEFAULT_APP_KEY_INDEX 0x0000
#define DEFAULT_NET_KEY_INDEX 0x0000
#define MESH_VENDOR_APP_BOUND_BIT (1U << 0)
#define MESH_LPN_FRIENDSHIP_BIT  (1U << 1)
#define MESH_SEND_COMPLETE_BIT   (1U << 2)

static uint8_t device_uuid[16] = {
    0xDE, 0xDE, 0x02, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static uint16_t s_net_idx = DEFAULT_NET_KEY_INDEX;
static uint16_t s_app_idx = DEFAULT_APP_KEY_INDEX;
static bool s_node_was_provisioned;
static EventGroupHandle_t s_mesh_events;
static esp_err_t s_last_send_status;

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_NOT_SUPPORTED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl = 7,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_model_op_t vendor_op[] = {
    ESP_BLE_MESH_MODEL_OP(MESH_VND_OP_SENSOR_DATA, MESH_SENSOR_PAYLOAD_LEN),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_client_t sensor_client;

static esp_ble_mesh_model_t vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(MESH_VND_CID_ESP,
                              MESH_VND_MODEL_CLI_ID,
                              vendor_op, NULL, &sensor_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vendor_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = MESH_VND_CID_ESP,
    .element_count = sizeof(elements) / sizeof(elements[0]),
    .elements = elements,
};

static esp_ble_mesh_prov_t provisioning = {
    .uuid = device_uuid,
};

static esp_err_t bluetooth_init(void)
{
    esp_bt_controller_config_t controller_config =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_bt_controller_init(&controller_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        return err;
    }

    return esp_bluedroid_enable();
}

static void provisioning_callback(esp_ble_mesh_prov_cb_event_t event,
                                  esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        if (param->node_prov_enable_comp.err_code == ESP_OK) {
            ESP_LOGI(TAG, "Provisioning start (PB-ADV)");
        } else {
            ESP_LOGE(TAG, "Provisioning enable failed: %d",
                     param->node_prov_enable_comp.err_code);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "Provisioning link opened, bearer=%d",
                 param->node_prov_link_open.bearer);
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "Provisioning link closed, reason=%u",
                 param->node_prov_link_close.reason);
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        s_net_idx = param->node_prov_complete.net_idx;
        ESP_LOGI(TAG, "Provisioned");
        ESP_LOGI(TAG, "Unicast address: 0x%04x",
                 param->node_prov_complete.addr);
        ESP_LOGI(TAG, "NetKey index: %u",
                 param->node_prov_complete.net_idx);
        ESP_LOGI(TAG,
                 "Composition Data: elements=1, CID=0x%04x, "
                 "models=Configuration Server(0x0000), "
                 "Vendor Client(0x0001)",
                 MESH_VND_CID_ESP);
        break;

    case ESP_BLE_MESH_LPN_FRIENDSHIP_ESTABLISH_EVT:
        ESP_LOGI(TAG, "Friendship established with Node 3 at 0x%04x",
                 param->lpn_friendship_establish.friend_addr);
        xEventGroupSetBits(s_mesh_events, MESH_LPN_FRIENDSHIP_BIT);
        break;

    case ESP_BLE_MESH_LPN_FRIENDSHIP_TERMINATE_EVT:
        ESP_LOGW(TAG, "Friendship with 0x%04x terminated",
                 param->lpn_friendship_terminate.friend_addr);
        xEventGroupClearBits(s_mesh_events, MESH_LPN_FRIENDSHIP_BIT);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "Provisioning data was reset");
        break;

    default:
        break;
    }
}

static void config_server_callback(esp_ble_mesh_cfg_server_cb_event_t event,
                                   esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        return;
    }

    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
        s_net_idx = param->value.state_change.appkey_add.net_idx;
        s_app_idx = param->value.state_change.appkey_add.app_idx;
        ESP_LOGI(TAG, "AppKey added: NetKey index=%u, AppKey index=%u",
                 s_net_idx, s_app_idx);
        return;
    }
    if (param->ctx.recv_op != ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
        return;
    }

    const esp_ble_mesh_state_change_cfg_model_app_bind_t *bind =
        &param->value.state_change.mod_app_bind;
    s_app_idx = bind->app_idx;
    if (bind->company_id == MESH_VND_CID_ESP &&
        bind->model_id == MESH_VND_MODEL_CLI_ID) {
        xEventGroupSetBits(s_mesh_events, MESH_VENDOR_APP_BOUND_BIT);
    }
    ESP_LOGI(TAG, "AppKey bound: AppKey index=%u, element=0x%04x, "
                  "model=0x%04x, company=0x%04x",
             bind->app_idx, bind->element_addr, bind->model_id,
             bind->company_id);
}

static void vendor_model_callback(esp_ble_mesh_model_cb_event_t event,
                                  esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code != ESP_OK) {
            ESP_LOGE(TAG, "Vendor sensor message send failed: %s",
                     esp_err_to_name(param->model_send_comp.err_code));
        } else {
            ESP_LOGI(TAG, "Vendor sensor message sent: opcode=0x%06" PRIx32,
                     param->model_send_comp.opcode);
        }
        s_last_send_status = param->model_send_comp.err_code;
        xEventGroupSetBits(s_mesh_events, MESH_SEND_COMPLETE_BIT);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(TAG, "Vendor publish message received");
        break;
    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err = esp_ble_mesh_register_prov_callback(provisioning_callback);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_register_config_server_callback(config_server_callback);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_register_custom_model_callback(vendor_model_callback);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_init(&provisioning, &composition);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vendor_models[0]);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh initialized");
    ESP_LOGI(TAG,
             "Composition Data: elements=1, CID=0x%04x, "
             "models=Configuration Server(0x0000), "
             "Vendor Client(0x0001)",
             MESH_VND_CID_ESP);

    s_node_was_provisioned = esp_ble_mesh_node_is_provisioned();
    if (s_node_was_provisioned) {
        ESP_LOGI(TAG, "Restored from NVS");
        ESP_LOGI(TAG, "Restored provisioned node");
        ESP_LOGI(TAG, "Unicast address: 0x%04x",
                 esp_ble_mesh_get_primary_element_address());
        return ESP_OK;
    }

    esp_ble_mesh_prov_bearer_t bearers =
        ESP_BLE_MESH_PROV_ADV;
    err = esp_ble_mesh_node_prov_enable(bearers);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Provisioning start");
    return ESP_OK;
}

static void put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)(value >> 8);
}

static esp_err_t publish_sensor_data(void)
{
    float temperature_c = 0.0f;
    float air_humidity_percent = 0.0f;
    int soil_adc_raw = 0;
    int soil_humidity_percent = 0;
    uint16_t temperature_tenths = 0;
    uint16_t humidity_tenths = 0;

    esp_err_t dht_err = ESP_ERR_TIMEOUT;
    for (int attempt = 0; attempt < 3; ++attempt) {
        dht_err = dht11_read(&temperature_c, &air_humidity_percent);
        if (dht_err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

#if defined(CONFIG_NODE2_SOIL_ADC_ENABLE) && CONFIG_NODE2_SOIL_ADC_ENABLE
    esp_err_t adc_err = ESP_ERR_TIMEOUT;
    int64_t adc_sum = 0;
    for (int sample = 0; sample < 10; ++sample) {
        int sample_raw = 0;
        esp_err_t sample_err = soil_adc_read(&sample_raw);
        if (sample_err == ESP_OK) {
            adc_sum += sample_raw;
        } else {
            adc_err = sample_err;
            break;
        }
        adc_err = ESP_OK;
    }
    if (adc_err == ESP_OK) {
        soil_adc_raw = (int)(adc_sum / 10);
    }
#else
    esp_err_t adc_err = ESP_ERR_NOT_SUPPORTED;
#endif

    ESP_LOGI(TAG, "========== SENSOR NODE 2 ==========");
    ESP_LOGI(TAG, "Sensor value:");
    if (dht_err == ESP_OK) {
        ESP_LOGI(TAG, "Air temperature: %.1f C", temperature_c);
        ESP_LOGI(TAG, "Air humidity: %.1f %%", air_humidity_percent);
        float temperature_encoded = temperature_c * 10.0f;
        if (temperature_encoded < 0) {
            temperature_tenths = (uint16_t)(int16_t)temperature_encoded;
        } else {
            temperature_tenths = (uint16_t)temperature_encoded;
        }
        if (air_humidity_percent >= 0 && air_humidity_percent <= 100) {
            humidity_tenths = (uint16_t)(air_humidity_percent * 10.0f);
        }
    } else {
        ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(dht_err));
    }

    if (adc_err == ESP_OK) {
        ESP_LOGI(TAG, "Soil ADC raw: %d", soil_adc_raw);
        if (soil_adc_percent(soil_adc_raw, &soil_humidity_percent)) {
            ESP_LOGI(TAG, "Soil moisture: %d %%", soil_humidity_percent);
        } else {
            ESP_LOGW(TAG, "Soil calibration unset; set dry/wet raw values "
                          "in menuconfig");
        }
    } else {
        ESP_LOGW(TAG, "Soil ADC disabled. Verify AO voltage is safe before "
                      "enabling it and connecting AO to GPIO34.");
    }
    ESP_LOGI(TAG, "==================================");

    uint8_t payload[MESH_SENSOR_PAYLOAD_LEN];
    payload[0] = (uint8_t)soil_humidity_percent;
    put_le16(&payload[1], temperature_tenths);
    put_le16(&payload[3], humidity_tenths);

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = s_net_idx,
        .app_idx = s_app_idx,
        .addr = GATEWAY_UNICAST_ADDR,
        .send_ttl = ESP_BLE_MESH_TTL_DEFAULT,
    };

    esp_err_t err = esp_ble_mesh_client_model_send_msg(
        sensor_client.model, &ctx, MESH_VND_OP_SENSOR_DATA,
        sizeof(payload), payload, 3000, false, ROLE_NODE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish vendor sensor data: %s",
                 esp_err_to_name(err));
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_mesh_events,
                                           MESH_SEND_COMPLETE_BIT,
                                           pdTRUE, pdTRUE,
                                           pdMS_TO_TICKS(5000));
    if ((bits & MESH_SEND_COMPLETE_BIT) == 0) {
        ESP_LOGE(TAG, "Timed out waiting for vendor message send completion");
        return ESP_ERR_TIMEOUT;
    }
    return s_last_send_status;
}

void app_main(void)
{
    s_mesh_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_mesh_events ? ESP_OK : ESP_ERR_NO_MEM);

    esp_err_t err = nvs_flash_init();
    ESP_ERROR_CHECK(err);

    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %d", (int)wakeup_cause);

    ESP_ERROR_CHECK(bluetooth_init());

    const uint8_t *bt_address = esp_bt_dev_get_address();
    if (bt_address == NULL) {
        ESP_LOGE(TAG, "Bluetooth device address unavailable");
        return;
    }
    memcpy(&device_uuid[4], bt_address, 6);
    ESP_LOGI(TAG, "Device UUID:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, device_uuid, sizeof(device_uuid),
                             ESP_LOG_INFO);

    ESP_ERROR_CHECK(dht11_init(GPIO_NUM_4));

#if defined(CONFIG_NODE2_SOIL_ADC_ENABLE) && CONFIG_NODE2_SOIL_ADC_ENABLE
    ESP_ERROR_CHECK(soil_adc_init());
#endif

    ESP_ERROR_CHECK(ble_mesh_init());

    if (!s_node_was_provisioned) {
        ESP_LOGI(TAG, "Waiting for provisioning; the node will not sleep");
        while (!esp_ble_mesh_node_is_provisioned()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        ESP_LOGI(TAG, "Waiting for Node 4 to bind the sensor AppKey");
        xEventGroupWaitBits(s_mesh_events, MESH_VENDOR_APP_BOUND_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);
    } else if (esp_ble_mesh_node_get_local_app_key(s_app_idx) == NULL) {
        ESP_LOGE(TAG, "Provisioned node has no AppKey; reprovision it with Node 4");
        return;
    }

    err = esp_ble_mesh_lpn_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable LPN: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Waiting for Node 3 Friend friendship");
    while ((xEventGroupWaitBits(s_mesh_events, MESH_LPN_FRIENDSHIP_BIT,
                                pdFALSE, pdTRUE, pdMS_TO_TICKS(10000)) &
            MESH_LPN_FRIENDSHIP_BIT) == 0) {
        ESP_LOGW(TAG, "No Friend yet; keeping Node 2 awake");
    }

    ESP_LOGI(TAG, "Provisioned node ready; publishing sensor data");
    err = publish_sensor_data();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sensor data was not delivered; keeping Node 2 awake");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Entering deep sleep for %" PRIu32 " seconds",
             (uint32_t)CONFIG_NODE2_SLEEP_INTERVAL_SEC);
    esp_sleep_enable_timer_wakeup(
        (uint64_t)CONFIG_NODE2_SLEEP_INTERVAL_SEC * 1000000ULL);
    esp_deep_sleep_start();
}
