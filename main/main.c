/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "spi.h"
#include "indication_task.h"
#include "ntp_task.h"
#include "driver/gpio.h"
#include "board_config.h"
#include "cli_task.h"
#include "nvs_fx.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"


#define LOGI(...) ESP_LOGI("MAIN", __VA_ARGS__)

void gpio_bsp_init(void) 
{
    gpio_config_t io_conf = {};

    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ( (1ULL << PWEN_PIN)  | (1ULL << LED_PIN) | (1ULL << IPHEN_PIN));
    io_conf.pull_up_en   = 0;
    gpio_config(&io_conf);
    gpio_set_level(LED_PIN, 1);
    gpio_set_level(IPHEN_PIN, 1);
    gpio_set_level(PWEN_PIN, 1);
}


void app_main(void)
{
    
    gpio_bsp_init();
    nvs_fx_init();
    spi0_init();
    indication_init(spi0_tx);
       
    ntp_task_init();
    cli_task_init();
    vTaskDelay(100);
    
    


    for(;;)
    {
        LOGI("Hearbeat\r\n");
        vTaskDelay(500);
        
    }
}
