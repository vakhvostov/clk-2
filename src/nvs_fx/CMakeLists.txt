idf_component_register(SRCS
                       "nvs_fx.c"
                       INCLUDE_DIRS
                       "."
                       "../"
                       REQUIRES driver nvs_flash esp_system)