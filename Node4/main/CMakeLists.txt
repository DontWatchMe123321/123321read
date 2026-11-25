idf_component_register(SRCS "main.c" "wifi_station.c" "mesh_bt_init.c" "ble_mesh_provisioner.c" "mqtt_gateway.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash esp_netif esp_event bt mqtt)
