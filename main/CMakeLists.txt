idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "." "../src"
                    REQUIRES spi indication_task ntp_task cli_task nvs_fx)
