/**
 * BLE Mesh Provisioner module untuk Node 4 (ESP32-S3).
 * Node 4 berperan sebagai Provisioner: memindai unprovisioned device,
 * mem-provision-nya, lalu mengonfigurasi (comp data, AppKey, model bind).
 *
 * Tahap ini: prov + config Generic OnOff Server node (polanya sama
 * dengan pump controller Node 1 kelak).
 */

#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#include "ble_mesh_provisioner.h"
#include "mesh_bt_init.h"
#include "mesh_sensor_protocol.h"
#include "mqtt_gateway.h"

#define CID_ESP             0x02E5

#define PROV_OWN_ADDR       0x0001
#define PROV_START_ADDRESS  0x0005

#define MSG_SEND_TTL        3
#define MSG_TIMEOUT         0

#define COMP_DATA_PAGE_0    0x00

#define APP_KEY_IDX         0x0000
#define APP_KEY_OCTET       0x12

#define MESH_BIND_MODEL_NONE    0
#define MESH_BIND_MODEL_ONOFF   1
#define MESH_BIND_MODEL_SENSOR  2

/* Filter UUID dua byte pertama: hanya device dengan UUID "DE.."
 * (mis. Node 1 pump controller menyebarkan UUID mulai 0xDE) */
static uint8_t dev_uuid_match[2] = {0xDE, 0xDE};

typedef struct {
    uint8_t  uuid[16];
    uint16_t unicast;
    uint8_t  elem_num;
    uint8_t  onoff;
    uint8_t  bind_model_kind;
    uint16_t bind_element;
} mesh_node_info_t;

static mesh_node_info_t nodes[CONFIG_BLE_MESH_MAX_PROV_NODES];

struct mesh_keys {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  app_key[16];
} prov_key;

static esp_ble_mesh_client_t config_client;
static esp_ble_mesh_client_t onoff_client;

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
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

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &onoff_client),
};

/* Vendor model server: menerima data sensor dari Node 2 (dan node lain)
 * lalu meneruskannya ke MQTT. */
static esp_ble_mesh_model_op_t vnd_sensor_ops[] = {
    ESP_BLE_MESH_MODEL_OP(MESH_VND_OP_SENSOR_DATA, MESH_SENSOR_PAYLOAD_LEN),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(MESH_VND_CID_ESP, MESH_VND_MODEL_SRV_ID,
                              vnd_sensor_ops, NULL, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid                = NULL,
    .prov_uuid           = NULL,
    .prov_unicast_addr   = PROV_OWN_ADDR,
    .prov_start_address  = PROV_START_ADDRESS,
    .prov_attention      = 0x00,
    .prov_algorithm      = 0x00,
    .prov_pub_key_oob    = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0x00,
    .flags               = 0x00,
    .iv_index            = 0x00,
};

/* ---------- tabel node ---------- */

static esp_err_t mesh_store_node_info(const uint8_t uuid[16], uint16_t unicast,
                                      uint8_t elem_num, uint8_t onoff_state)
{
    int i;

    if (!uuid || !ESP_BLE_MESH_ADDR_IS_UNICAST(unicast)) {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0; i < ARRAY_SIZE(nodes); i++) {
        if (!memcmp(nodes[i].uuid, uuid, 16)) {
            nodes[i].unicast = unicast;
            nodes[i].elem_num = elem_num;
            nodes[i].onoff = onoff_state;
            nodes[i].bind_model_kind = MESH_BIND_MODEL_NONE;
            nodes[i].bind_element = ESP_BLE_MESH_ADDR_UNASSIGNED;
            return ESP_OK;
        }
    }

    for (i = 0; i < ARRAY_SIZE(nodes); i++) {
        if (nodes[i].unicast == ESP_BLE_MESH_ADDR_UNASSIGNED) {
            memcpy(nodes[i].uuid, uuid, 16);
            nodes[i].unicast = unicast;
            nodes[i].elem_num = elem_num;
            nodes[i].onoff = onoff_state;
            nodes[i].bind_model_kind = MESH_BIND_MODEL_NONE;
            nodes[i].bind_element = ESP_BLE_MESH_ADDR_UNASSIGNED;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static mesh_node_info_t *mesh_get_node_info(uint16_t unicast)
{
    int i;

    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(unicast)) {
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(nodes); i++) {
        if (nodes[i].unicast <= unicast &&
                nodes[i].unicast + nodes[i].elem_num > unicast) {
            return &nodes[i];
        }
    }
    return NULL;
}

static void mesh_restore_app_node_table(void)
{
    const esp_ble_mesh_node_t **entry =
        esp_ble_mesh_provisioner_get_node_table_entry();

    if (!entry) {
        return;
    }

    for (uint16_t i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++) {
        const esp_ble_mesh_node_t *node = entry[i];
        if (node) {
            mesh_store_node_info(node->dev_uuid, node->unicast_addr,
                                 node->element_num, 0);
            ESP_LOGI(TAG_BLE_MESH,
                     "Restored app node table: unicast 0x%04x, elem %u",
                     node->unicast_addr, node->element_num);
        }
    }
}

static esp_err_t mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                     mesh_node_info_t *node,
                                     esp_ble_mesh_model_t *model, uint32_t opcode)
{
    if (!common || !node || !model) {
        return ESP_ERR_INVALID_ARG;
    }
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = node->unicast;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = MSG_TIMEOUT;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common->msg_role = ROLE_PROVISIONER;
#endif
    return ESP_OK;
}

static esp_err_t mesh_select_bind_model(mesh_node_info_t *node,
                                        const uint8_t *data, uint16_t len)
{
    size_t offset = 10;
    uint8_t selected_kind = MESH_BIND_MODEL_NONE;
    uint16_t selected_element = ESP_BLE_MESH_ADDR_UNASSIGNED;
    uint8_t onoff_found = 0;
    uint16_t onoff_element = ESP_BLE_MESH_ADDR_UNASSIGNED;

    if (!node || !data || len < offset) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t element = 0; element < node->elem_num; element++) {
        if (offset + 4 > len) {
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t sig_count = data[offset + 2];
        uint8_t vendor_count = data[offset + 3];
        offset += 4;

        for (uint8_t i = 0; i < sig_count; i++) {
            if (offset + 2 > len) {
                return ESP_ERR_INVALID_SIZE;
            }
            uint16_t model_id = data[offset] | ((uint16_t)data[offset + 1] << 8);
            offset += 2;
            if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
                onoff_found = 1;
                onoff_element = node->unicast + element;
            }
        }

        for (uint8_t i = 0; i < vendor_count; i++) {
            if (offset + 4 > len) {
                return ESP_ERR_INVALID_SIZE;
            }
            uint16_t company_id = data[offset] |
                                  ((uint16_t)data[offset + 1] << 8);
            uint16_t model_id = data[offset + 2] |
                                ((uint16_t)data[offset + 3] << 8);
            offset += 4;
            if (company_id == MESH_VND_CID_ESP &&
                    model_id == MESH_VND_MODEL_CLI_ID) {
                selected_kind = MESH_BIND_MODEL_SENSOR;
                selected_element = node->unicast + element;
            }
        }
    }

    if (selected_kind == MESH_BIND_MODEL_NONE && onoff_found) {
        selected_kind = MESH_BIND_MODEL_ONOFF;
        selected_element = onoff_element;
    }
    if (selected_kind == MESH_BIND_MODEL_NONE) {
        return ESP_ERR_NOT_FOUND;
    }

    node->bind_model_kind = selected_kind;
    node->bind_element = selected_element;
    ESP_LOGI(TAG_BLE_MESH, "node 0x%04x target model: %s at element 0x%04x",
             node->unicast,
             selected_kind == MESH_BIND_MODEL_SENSOR ?
             "vendor sensor client" : "Generic OnOff Server",
             selected_element);
    return ESP_OK;
}

esp_err_t ble_mesh_node_configure(uint16_t unicast)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_get_state_t get_state = {0};
    mesh_node_info_t *node = mesh_get_node_info(unicast);
    esp_err_t err;

    if (!node) {
        return ESP_ERR_NOT_FOUND;
    }

    mesh_set_msg_common(&common, node, config_client.model,
                        ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get_state.comp_data_get.page = COMP_DATA_PAGE_0;
    err = esp_ble_mesh_config_client_get_state(&common, &get_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_BLE_MESH,
                 "Composition Data Get failed for node 0x%04x: %s",
                 unicast, esp_err_to_name(err));
    }
    return err;
}

/* ---------- provisioning callback ---------- */

static esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                               uint16_t unicast, uint8_t elem_num, uint16_t net_idx)
{
    mesh_node_info_t *node = NULL;
    char name[11] = {0};
    esp_err_t err;

    ESP_LOGI(TAG_BLE_MESH, "node idx 0x%x, unicast 0x%04x, elem %d, net_idx 0x%02x",
             node_idx, unicast, elem_num, net_idx);
    ESP_LOGI(TAG_BLE_MESH, "device uuid: %s", bt_hex(uuid, 16));

    snprintf(name, sizeof(name), "NODE-%d", node_idx);
    err = esp_ble_mesh_provisioner_set_node_name(node_idx, name);
    if (err) {
        ESP_LOGE(TAG_BLE_MESH, "Set node name failed");
    }

    err = mesh_store_node_info(uuid, unicast, elem_num, 0);
    if (err) {
        ESP_LOGE(TAG_BLE_MESH, "Store node info failed");
        return ESP_FAIL;
    }

    node = mesh_get_node_info(unicast);
    if (!node) {
        ESP_LOGE(TAG_BLE_MESH, "Get node info failed");
        return ESP_FAIL;
    }

    /* Read the Composition Data and bind the AppKey to a model it supports. */
    err = ble_mesh_node_configure(unicast);
    if (err) {
        ESP_LOGE(TAG_BLE_MESH, "Send comp data get failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    esp_err_t err;

    ESP_LOGI(TAG_BLE_MESH, "unprov dev: addr %s, uuid %s, bearer %s",
             bt_hex(addr, BD_ADDR_LEN), bt_hex(dev_uuid, 16),
             (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");
    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = (esp_ble_mesh_addr_type_t)addr_type;
    memcpy(add_dev.uuid, dev_uuid, 16);
    add_dev.oob_info = oob_info;
    add_dev.bearer = (esp_ble_mesh_prov_bearer_t)bearer;

    err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
            (esp_ble_mesh_dev_add_flag_t)(ADD_DEV_RM_AFTER_PROV_FLAG |
             ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG));
    if (err) {
        ESP_LOGE(TAG_BLE_MESH, "Add unprovisioned device failed");
    }
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid,
                            param->provisioner_recv_unprov_adv_pkt.addr,
                            param->provisioner_recv_unprov_adv_pkt.addr_type,
                            param->provisioner_recv_unprov_adv_pkt.oob_info,
                            param->provisioner_recv_unprov_adv_pkt.adv_type,
                            param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG_BLE_MESH, "prov link open (%s)",
                 param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ?
                 "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG_BLE_MESH, "prov link close, reason 0x%02x",
                 param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        prov_complete(param->provisioner_prov_complete.node_idx,
                      param->provisioner_prov_complete.device_uuid,
                      param->provisioner_prov_complete.unicast_addr,
                      param->provisioner_prov_complete.element_num,
                      param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        if (param->provisioner_add_app_key_comp.err_code == ESP_OK) {
            prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
            esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                PROV_OWN_ADDR, prov_key.app_idx,
                ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, ESP_BLE_MESH_CID_NVAL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_BLE_MESH, "Bind appkey to local model failed");
            } else {
                ESP_LOGI(TAG_BLE_MESH, "AppKey idx 0x%02x bound to local GenOnOff CLI",
                         prov_key.app_idx);
            }
            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                PROV_OWN_ADDR, prov_key.app_idx,
                MESH_VND_MODEL_SRV_ID, MESH_VND_CID_ESP);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_BLE_MESH,
                         "Bind AppKey to local vendor sensor server failed: %s",
                         esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG_BLE_MESH,
                         "AppKey idx 0x%02x bound to local vendor sensor server",
                         prov_key.app_idx);
            }
        }
        break;
    default:
        break;
    }
}

/* ---------- config client callback ---------- */

static void example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                              esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    mesh_node_info_t *node = NULL;
    uint32_t opcode;
    uint16_t addr;
    esp_err_t err;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG_BLE_MESH, "cfg cli evt 0x%02x, err 0x%02x, addr 0x%04x, op 0x%04" PRIx32,
             event, param->error_code, addr, opcode);

    if (param->error_code) {
        return;
    }

    node = mesh_get_node_info(addr);
    if (!node) {
        ESP_LOGE(TAG_BLE_MESH, "cfg cli: node 0x%04x not found", addr);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            esp_ble_mesh_cfg_client_set_state_t set_state = {0};
            struct net_buf_simple *comp_data =
                param->status_cb.comp_data_status.composition_data;

            if (!comp_data) {
                ESP_LOGE(TAG_BLE_MESH,
                         "Composition Data missing for node 0x%04x", addr);
                return;
            }
            ESP_LOGI(TAG_BLE_MESH, "comp data %s",
                     bt_hex(comp_data->data, comp_data->len));
            err = mesh_select_bind_model(node, comp_data->data, comp_data->len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_BLE_MESH,
                         "No supported AppKey model in node 0x%04x Composition Data (%s)",
                         addr, esp_err_to_name(err));
                return;
            }

            mesh_set_msg_common(&common, node, config_client.model,
                                ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set_state.app_key_add.net_idx = prov_key.net_idx;
            set_state.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err) {
                ESP_LOGE(TAG_BLE_MESH, "Config AppKey Add failed");
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            uint8_t status = param->status_cb.appkey_status.status;
            if (status != ESP_BLE_MESH_CFG_STATUS_SUCCESS) {
                ESP_LOGE(TAG_BLE_MESH,
                         "AppKey Add rejected by node 0x%04x: status 0x%02x",
                         addr, status);
                return;
            }
            if (node->bind_model_kind == MESH_BIND_MODEL_NONE) {
                ESP_LOGE(TAG_BLE_MESH,
                         "No target model selected for node 0x%04x", addr);
                return;
            }

            esp_ble_mesh_cfg_client_set_state_t set_state = {0};
            mesh_set_msg_common(&common, node, config_client.model,
                                ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set_state.model_app_bind.element_addr = node->bind_element;
            set_state.model_app_bind.model_app_idx = prov_key.app_idx;
            if (node->bind_model_kind == MESH_BIND_MODEL_SENSOR) {
                set_state.model_app_bind.model_id = MESH_VND_MODEL_CLI_ID;
                set_state.model_app_bind.company_id = MESH_VND_CID_ESP;
            } else {
                set_state.model_app_bind.model_id =
                    ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
                set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
            }
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err) {
                ESP_LOGE(TAG_BLE_MESH, "Config Model App Bind failed: %s",
                         esp_err_to_name(err));
            }
        } else if (opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            uint8_t status = param->status_cb.model_app_status.status;
            if (status != ESP_BLE_MESH_CFG_STATUS_SUCCESS) {
                ESP_LOGE(TAG_BLE_MESH,
                         "Model App Bind rejected by node 0x%04x: status 0x%02x",
                         addr, status);
                return;
            }
            if (node->bind_model_kind == MESH_BIND_MODEL_SENSOR) {
                ESP_LOGI(TAG_BLE_MESH,
                         "Vendor Sensor Client bound to AppKey on node 0x%04x",
                         addr);
                return;
            }

            esp_ble_mesh_generic_client_get_state_t get_state = {0};
            mesh_set_msg_common(&common, node, onoff_client.model,
                                ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET);
            err = esp_ble_mesh_generic_client_get_state(&common, &get_state);
            if (err) {
                ESP_LOGE(TAG_BLE_MESH, "Generic OnOff Get failed");
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        ESP_LOGW(TAG_BLE_MESH, "config msg timeout, op 0x%04" PRIx32, opcode);
        break;
    default:
        break;
    }
}

/* ---------- generic client callback ---------- */

static void example_ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                               esp_ble_mesh_generic_client_cb_param_t *param)
{
    mesh_node_info_t *node = NULL;
    uint32_t opcode;
    uint16_t addr;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    node = mesh_get_node_info(addr);
    if (!node) {
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            node->onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG_BLE_MESH, "node 0x%04x onoff = %d", addr, node->onoff);
            /* Teruskan status pompa ke MQTT (retained) */
            mqtt_gateway_publish_pump_status(addr, node->onoff);
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        if (opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
            node->onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG_BLE_MESH, "node 0x%04x onoff set -> %d", addr, node->onoff);
            /* Teruskan status pompa ke MQTT (retained) */
            mqtt_gateway_publish_pump_status(addr, node->onoff);
        }
        break;
    default:
        break;
    }
}

/* ---------- custom (vendor) model callback ---------- */

static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {
        uint32_t opcode = param->model_operation.opcode;
        esp_ble_mesh_msg_ctx_t *ctx = param->model_operation.ctx;
        uint8_t *msg = param->model_operation.msg;
        uint16_t len = param->model_operation.length;

        if (opcode == MESH_VND_OP_SENSOR_DATA) {
            if (len < MESH_SENSOR_PAYLOAD_LEN) {
                ESP_LOGW(TAG_BLE_MESH, "sensor payload too short: %u", len);
                return;
            }
            ESP_LOGI(TAG_BLE_MESH,
                     "sensor data from 0x%04x: soil=%u%% temp=%.1fC hum=%.1f%%",
                     ctx->addr, msg[0],
                     (int16_t)(msg[1] | (msg[2] << 8)) / 10.0f,
                     (uint16_t)(msg[3] | (msg[4] << 8)) / 10.0f);
            /* Teruskan ke MQTT gateway */
            mqtt_gateway_publish_sensor(ctx->addr, msg, len);
        } else {
            ESP_LOGW(TAG_BLE_MESH, "unknown vendor opcode 0x%06" PRIx32, opcode);
        }
        break;
    }
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        break;
    default:
        break;
    }
}

/* ---------- init & public API ---------- */

esp_err_t ble_mesh_node_onoff_set(uint16_t unicast, uint8_t onoff)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_generic_client_set_state_t set_state = {0};
    mesh_node_info_t *node = mesh_get_node_info(unicast);
    esp_err_t err;

    if (!node) {
        return ESP_ERR_NOT_FOUND;
    }

    mesh_set_msg_common(&common, node, onoff_client.model,
                        ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET);
    set_state.onoff_set.op_en = false;
    set_state.onoff_set.onoff = onoff ? 1 : 0;
    static uint8_t tid = 0;
    set_state.onoff_set.tid = tid++;

    err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
    if (err) {
        ESP_LOGE(TAG_BLE_MESH, "Gen OnOff Set failed err=%d", err);
    }
    return err;
}

esp_err_t ble_mesh_provisioner_init(void)
{
    esp_err_t err;
    static uint8_t s_dev_uuid[16] = {0};

    prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    prov_key.app_idx = APP_KEY_IDX;
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));

    memset(nodes, 0, sizeof(nodes));

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    /* UUID provisioner: 2 byte pertama bebas, sisanya alamat BT.
     * Kedua peran Mesh membutuhkan UUID pada struktur provisioning. */
    mesh_bt_get_dev_uuid(s_dev_uuid);
    provision.uuid = s_dev_uuid;
    provision.prov_uuid = s_dev_uuid;

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_BLE_MESH, "Failed to init mesh stack (err %d)", err);
        return err;
    }

    mesh_restore_app_node_table();

    /* Filter: hanya device dengan 2 byte pertama UUID = DE DE */
    err = esp_ble_mesh_provisioner_set_dev_uuid_match(dev_uuid_match, sizeof(dev_uuid_match), 0x0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_BLE_MESH, "Failed set uuid match (err %d)", err);
        return err;
    }
    ESP_LOGI(TAG_BLE_MESH, "uuid match set: DE DE (offset 0)");

    err = esp_ble_mesh_provisioner_prov_enable(
        (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_BLE_MESH, "Failed enable provisioner (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_provisioner_add_local_app_key(prov_key.app_key,
                                                     prov_key.net_idx, prov_key.app_idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_BLE_MESH, "Failed add local AppKey (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG_BLE_MESH, "BLE Mesh Provisioner initialized");
    return ESP_OK;
}
