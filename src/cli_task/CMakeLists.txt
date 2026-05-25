idf_component_register(SRCS
                       "cli_task.c"
                       INCLUDE_DIRS
                       "."
                       "../"
                       REQUIRES driver nvs_fx esp_system console vfs ntp_task indication_task)
