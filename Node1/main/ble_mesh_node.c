/**
 * @file ble_mesh_node.c
 * @brief BLE Mesh Node 1 — Generic OnOff Server (pompa DC).
 *
 * Parameter wajib yang cocok dengan Provisioner Node 4:
 *   - dev_uuid[0..1] = 0xDE 0xDE (filter UUID provisioner)
 *   - dev_uuid[2]    = 0x01 (prefix unik Node 1; Node 2/3/4 pakai 02/03/04)
 *   - No OOB (output 0, input 0) — provisioner pakai static OOB NULL
 *   - PB-ADV bearer
 *   - 1 element: CFG_SRV + GEN_ONOFF_SRV, CID 0x02E5
 *
 * Unicast address TIDAK ditentukan di sini — Node 4 yang menetapkan
 * (0x0006 atau berikutnya), dibaca dari callback provisioning.
 */
#include "ble_mesh_node.h"

#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "board.h"
#include "app_state.h"
#include "sdkconfig.h"

#define TAG "mesh_node"

#define CID_ESP 0x02E5

/*
 * Device UUID 16 byte — HARUS diawali 0xDE 0xDE agar tidak diabaikan
 * provisioner Node 4. Byte[2] = 0x01 membedakan dari Node 2/3/4.
 * Non-static: dicetak di main.c saat boot.
 */
uint8_t dev_uuid[16] = {
    0xDE, 0xDE, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ------------------------------------------------------------------ */
/* Composition: 1 element = CFG_SRV + GEN_ONOFF_SRV                    */
/* ------------------------------------------------------------------ */

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,       /* Node 1 murni server */
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

/* Semua respons ditangani aplikasi (RSP_BY_APP) agar bisa log
 * ONOFF_STATUS_SENT dan tidak ada dobel Status. */
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub_0, 2 + 3, ROLE_NODE);
static esp_ble_mesh_gen_onoff_srv_t onoff_server_0 = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    },
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub_0, &onoff_server_0),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

/* No OOB — cocok dengan provisioner (static OOB NULL) */
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

/* ------------------------------------------------------------------ */
/* OnOff handling                                                      */
/* ------------------------------------------------------------------ */

static void send_onoff_status(esp_ble_mesh_model_t *model,
                              esp_ble_mesh_msg_ctx_t *ctx)
{
    esp_err_t err = esp_ble_mesh_server_model_send_msg(model, ctx,
            ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,
            sizeof(onoff_server_0.state.onoff), &onoff_server_0.state.onoff);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ONOFF_STATUS_SENT (onoff=%u, dst=0x%04x)",
                 onoff_server_0.state.onoff, ctx->addr);
    } else {
        ESP_LOGE(TAG, "Gagal kirim OnOff Status: %s", esp_err_to_name(err));
    }
}

/* Terapkan state: mesh state + relay + NVS. TIDAK mengirim status. */
static void apply_onoff(uint8_t onoff)
{
    uint8_t new_state = onoff ? 1 : 0;

    if (onoff_server_0.state.onoff == new_state) {
        return; /* tidak ada perubahan — jangan spin relay */
    }

    onoff_server_0.state.onoff = new_state;
    board_relay_set(new_state);
    ESP_LOGI(TAG, "NODE1_%s", new_state ? "ON" : "OFF");

    if (app_state_save_onoff(new_state) != ESP_OK) {
        ESP_LOGW(TAG, "OnOff gagal disimpan ke NVS (state tetap jalan)");
    }
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

static void log_provisioned_info(uint16_t net_idx, uint16_t addr,
                                 uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "PROVISIONED");
    ESP_LOGI(TAG, "Unicast address: 0x%04x", addr);
    ESP_LOGI(TAG, "NetKeyIndex    : 0x%04x", net_idx);
    ESP_LOGI(TAG, "Flags: 0x%02x, IVindex: 0x%08" PRIx32, flags, iv_index);
    /* AppKeyIndex dicetak di config server callback (APP_KEY_ADD / BIND). */
}

static void restore_after_boot(void)
{
    ESP_LOGI(TAG, "RESTORED_MESH_STATE (sudah provisioned)");
    ESP_LOGI(TAG, "Unicast address: 0x%04x",
             esp_ble_mesh_get_primary_element_address());

    uint8_t restored = 0;
    if (app_state_load_onoff(&restored) != ESP_OK) {
        restored = 0;
    }
    ESP_LOGI(TAG, "RESTORED_ONOFF_STATE: %u", restored);

#if CONFIG_NODE1_RESTORE_ONOFF_AFTER_REBOOT
    /* Kontinuitas: pompa kembali ke state terakhir sebelum power-loss.
     * Satu-satunya jalur yang boleh menyalakan relay saat boot. */
    onoff_server_0.state.onoff = restored;
    if (restored) {
        board_relay_set(1);
        ESP_LOGI(TAG, "NODE1_ON (restored setelah reboot)");
    }
#else
    /* Fail-safe murni (default): pompa selalu OFF setelah reboot,
     * state mesh & NVS disinkronkan ke OFF. */
    if (restored != 0) {
        ESP_LOGW(TAG, "State tersimpan ON, di-override ke OFF (fail-safe)");
        app_state_save_onoff(0);
    }
    onoff_server_0.state.onoff = 0;
    board_relay_set(0);
#endif
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                     esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d",
                 param->prov_register_comp.err_code);
        if (param->prov_register_comp.err_code == ESP_OK &&
                esp_ble_mesh_node_is_provisioned()) {
            restore_after_boot();
        } else {
            ESP_LOGI(TAG, "Node UNPROVISIONED, relay OFF, menunggu Node 4");
            board_relay_set(0);
        }
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "PROV_LINK_OPEN bearer: %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ?
                 "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "PROV_LINK_CLOSE bearer: %s",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ?
                 "PB-ADV" : "PB-GATT");
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        /* Node 4 selesai memprovision. Alamat dari parameter callback,
         * bukan ditentukan sendiri (Node 4 assign 0x0006 atau berikutnya). */
        log_provisioned_info(param->node_prov_complete.net_idx,
                             param->node_prov_complete.addr,
                             param->node_prov_complete.flags,
                             param->node_prov_complete.iv_index);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "NODE_PROV_RESET: kembali unprovisioned, relay OFF");
        board_relay_set(0);
        onoff_server_0.state.onoff = 0;
        break;

    default:
        break;
    }
}

static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                      esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        return;
    }

    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        ESP_LOGI(TAG, "AppKey ditambahkan: NetKeyIndex 0x%04x, AppKeyIndex 0x%04x",
                 param->value.state_change.appkey_add.net_idx,
                 param->value.state_change.appkey_add.app_idx);
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        /* SIG model: cek model_id saja (0x1000 = Generic OnOff Server). */
        if (param->value.state_change.mod_app_bind.model_id ==
                    ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV) {
            ESP_LOGI(TAG, "Generic OnOff Server BOUND: elem 0x%04x, "
                          "AppKeyIndex 0x%04x",
                     param->value.state_change.mod_app_bind.element_addr,
                     param->value.state_change.mod_app_bind.app_idx);
        }
        break;

    default:
        break;
    }
}

static void ble_mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                       esp_ble_mesh_generic_server_cb_param_t *param)
{
    esp_ble_mesh_gen_onoff_srv_t *srv =
        (esp_ble_mesh_gen_onoff_srv_t *)param->model->user_data;
    if (srv != &onoff_server_0) {
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
            ESP_LOGI(TAG, "ONOFF_GET_RECEIVED (src=0x%04x, dst=0x%04x)",
                     param->ctx.addr, param->ctx.recv_dst);
            send_onoff_status(param->model, &param->ctx);
        }
        break;

    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {
            ESP_LOGI(TAG, "ONOFF_SET_RECEIVED (onoff=%u, tid=0x%02x, "
                          "src=0x%04x, dst=0x%04x, %s)",
                     param->value.set.onoff.onoff,
                     param->value.set.onoff.tid,
                     param->ctx.addr, param->ctx.recv_dst,
                     param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ?
                     "ACK" : "UNACK");

            /* TID & sequence (replay protection) ditangani access layer. */
            apply_onoff(param->value.set.onoff.onoff);

            /* Balas Status hanya untuk SET yang di-ACK (sesuai spesifikasi). */
            if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
                send_onoff_status(param->model, &param->ctx);
            }
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

esp_err_t ble_mesh_node_init(void)
{
    esp_err_t err;

    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);
    esp_ble_mesh_register_generic_server_callback(ble_mesh_generic_server_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mesh init gagal: %s", esp_err_to_name(err));
        return err;
    }

    if (!esp_ble_mesh_node_is_provisioned()) {
        /* PB-ADV sesuai konfigurasi provisioner Node 4. */
        err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "prov enable gagal: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "BLE Mesh Node aktif, advertise unprovisioned (PB-ADV)");
    } else {
        ESP_LOGI(TAG, "BLE Mesh Node aktif (sudah provisioned, state restored)");
    }

    return ESP_OK;
}
