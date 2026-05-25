#include "nvs_fx.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "stdbool.h"
#include "string.h"

static SemaphoreHandle_t nvs_mutex;

#define LOGI(...) ESP_LOGI("NVS", __VA_ARGS__)

int nvs_fx_init()
{
    int retval = -1;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        LOGI("Failed to init NVS");
        goto exit;
    }
    nvs_mutex = xSemaphoreCreateMutex();
    if (NULL == nvs_mutex) {
        LOGI("Failed to init MTX");
        goto exit;
    }
    retval = 0;
    LOGI("NVS Initialized");
exit:
    return retval;
}

int nvs_fx_get(const char* key, char* out, size_t size)
{
    xSemaphoreTake(nvs_mutex, portMAX_DELAY);
    nvs_handle_t nvsh;
    nvs_open("config", NVS_READONLY, &nvsh);
    nvs_get_str(nvsh, key, out, &size);
    nvs_close(nvsh);
    xSemaphoreGive(nvs_mutex);
    return 0;
}

int nvs_fx_set(const char* key, char* val)
{
    xSemaphoreTake(nvs_mutex, portMAX_DELAY);
    nvs_handle_t nvsh;
    nvs_open("config", NVS_READWRITE, &nvsh);
    nvs_set_str(nvsh, key, val);
    nvs_commit(nvsh);
    nvs_close(nvsh);
    xSemaphoreGive(nvs_mutex);
    return 0;
}
