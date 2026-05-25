idf_component_register(SRCS
                       "indication_task.c"
                       INCLUDE_DIRS
                       "."
                       "../"
                       REQUIRES driver esp_adc nvs_fx)