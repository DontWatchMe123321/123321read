
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#define TAG "NODE_3"
#define CID_ESP 0x02E5

// ========================================
// DEVICE UUID NODE 3
// ========================================

static uint8_t dev_uuid[16] = {
    0xDE, 0xDE, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03
};

// ========================================
// GENERIC ONOFF SERVER
// ========================================

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub, 2 + 3, ROLE_NODE);

static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
};

// ========================================
// CONFIGURATION SERVER
// ========================================

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),

#if defined(CONFIG_BLE_MESH_RELAY)
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
#else
    .relay = ESP_BLE_MESH_RELAY_NOT_SUPPORTED,
#endif

    .beacon = ESP_BLE_MESH_BEACON_ENABLED,

#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif

#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif

    .default_ttl = 7,
};

// ========================================
// BLE MESH MODEL
// ========================================

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(
        0,
        root_models,
        ESP_BLE_MESH_MODEL_NONE
    ),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = sizeof(elements) / sizeof(elements[0]),
    .elements = elements,
};

// ========================================
// PROVISIONING CONFIGURATION
// ========================================

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

static esp_err_t status_led_init(void)
{
#if CONFIG_NODE3_STATUS_LED_GPIO >= 0
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_NODE3_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level(CONFIG_NODE3_STATUS_LED_GPIO,
                          CONFIG_NODE3_STATUS_LED_ACTIVE_LOW ? 1 : 0);
#else
    return ESP_OK;
#endif
}

static void status_led_set(uint8_t onoff)
{
#if CONFIG_NODE3_STATUS_LED_GPIO >= 0
    esp_err_t err = gpio_set_level(
        CONFIG_NODE3_STATUS_LED_GPIO,
        CONFIG_NODE3_STATUS_LED_ACTIVE_LOW ? !onoff : onoff
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Status LED update failed: %s", esp_err_to_name(err));
    }
#else
    (void)onoff;
#endif
}

// ========================================
// PROVISIONING CALLBACK
// ========================================

static void provisioning_callback(
    esp_ble_mesh_prov_cb_event_t event,
    esp_ble_mesh_prov_cb_param_t *param
) {
    switch (event) {

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        if (param->node_prov_enable_comp.err_code == ESP_OK) {
            ESP_LOGI(TAG, "Provisioning enabled");
        } else {
            ESP_LOGE(TAG, "Provisioning enable failed: %d",
                     param->node_prov_enable_comp.err_code);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "Provisioning link opened over %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV
                     ? "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "Provisioning link closed over %s (reason %u)",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV
                     ? "PB-ADV" : "PB-GATT",
                 param->node_prov_link_close.reason);
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "================================");
        ESP_LOGI(TAG, "NODE 3 SUCCESSFULLY PROVISIONED");
        ESP_LOGI(TAG, "Network Index: %u",
                 param->node_prov_complete.net_idx);
        ESP_LOGI(TAG, "Primary Address: 0x%04x",
                 param->node_prov_complete.addr);
        ESP_LOGI(TAG, "================================");
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "Node provisioning reset");
        break;

    default:
        ESP_LOGI(TAG, "Provisioning event: %d", event);
        break;
    }
}

static void generic_server_callback(
    esp_ble_mesh_generic_server_cb_event_t event,
    esp_ble_mesh_generic_server_cb_param_t *param
) {
    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            ESP_LOGI(TAG, "Generic OnOff Get: state=%s",
                     onoff_server.state.onoff ? "ON" : "OFF");
        }
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
            uint8_t onoff = param->value.set.onoff.onoff;
            onoff_server.state.onoff = onoff;
            onoff_server.state.target_onoff = onoff;

            ESP_LOGI(TAG, "Generic OnOff Set: state=%s, source=0x%04x",
                     onoff ? "ON" : "OFF", param->ctx.addr);
            status_led_set(onoff);

            if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
                esp_err_t err = esp_ble_mesh_server_model_send_msg(
                    param->model,
                    &param->ctx,
                    ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,
                    sizeof(onoff_server.state.onoff),
                    &onoff_server.state.onoff
                );
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "OnOff Status response failed: %s",
                             esp_err_to_name(err));
                }
            }
        }
        break;

    default:
        break;
    }
}

// ========================================
// BLUETOOTH INITIALIZATION
// ========================================

static esp_err_t bluetooth_init(void)
{
    esp_bt_controller_config_t bt_cfg =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bluedroid_config_t bluedroid_cfg =
        BT_BLUEDROID_INIT_CONFIG_DEFAULT();

    esp_err_t err;

    err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Classic Bluetooth memory release failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Controller init failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Controller enable failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bluetooth initialized successfully");

    return ESP_OK;
}

// ========================================
// BLE MESH INITIALIZATION
// ========================================

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err;

    err = esp_ble_mesh_register_generic_server_callback(
        generic_server_callback
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register Generic Server callback failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_ble_mesh_register_prov_callback(
        provisioning_callback
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register provisioning callback failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_ble_mesh_init(
        &provision,
        &composition
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh initialization failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh initialized");

    if (esp_ble_mesh_node_is_provisioned()) {
        ESP_LOGI(TAG, "Node already provisioned; mesh settings restored");
        return ESP_OK;
    }

    esp_ble_mesh_prov_bearer_t bearers = ESP_BLE_MESH_PROV_ADV;
#if defined(CONFIG_BLE_MESH_PB_GATT)
    bearers = (esp_ble_mesh_prov_bearer_t)(
        bearers | ESP_BLE_MESH_PROV_GATT
    );
#endif

    err = esp_ble_mesh_node_prov_enable(bearers);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Provisioning enable failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Node 3 is waiting for Provisioner");

    return ESP_OK;
}

// ========================================
// MAIN APPLICATION
// ========================================

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "================================");
    ESP_LOGI(TAG, " NODE 3 SMART IRRIGATION");
    ESP_LOGI(TAG, " ESP32-S3 BLE MESH ONOFF SERVER");
    ESP_LOGI(TAG, "================================");

    // Initialize NVS
    err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    // Initialize Bluetooth
    err = bluetooth_init();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization error");
        return;
    }

    memcpy(dev_uuid + 4, esp_bt_dev_get_address(), 6);
    ESP_LOGI(TAG, "Device UUID (filter prefix DE DE):");
    ESP_LOG_BUFFER_HEX(TAG, dev_uuid, sizeof(dev_uuid));

    ESP_ERROR_CHECK(status_led_init());

    // Initialize BLE Mesh
    err = ble_mesh_init();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE Mesh initialization error");
        return;
    }

    while (1) {
        ESP_LOGI(TAG, "Node 3 running...");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
