idf_component_register(SRCS
                       "ntp_task.c"
                       INCLUDE_DIRS
                       "."
                       "../"
                       REQUIRES driver indication_task esp_wifi nvs_fx esp_system lwip esp_netif)