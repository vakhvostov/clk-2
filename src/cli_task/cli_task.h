#pragma once
#include "freertos/FreeRTOS.h"

#define CLI_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)

int cli_task_init(void);