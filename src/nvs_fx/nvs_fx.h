#pragma once

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "stdbool.h"

int nvs_fx_init();
int nvs_fx_get(const char* key, char* out, size_t size);
int nvs_fx_set(const char* key, char* val);