# main/CMakeLists.txt — Node1_pump
idf_component_register(
    SRCS "ble_mesh_node.c" "mesh_bt_init.c" "board.c" "main.c" "app_state.c"
    INCLUDE_DIRS "."
)
