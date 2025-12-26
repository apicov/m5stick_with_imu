idf_component_register(SRCS "m5stick_mesh_imu.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES ble_mesh_node bt nvs_flash)
