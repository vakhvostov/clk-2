#include "indication_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdbool.h"
#include "sys/time.h"
#include "time.h"
#include "esp_log.h"

#define NIXIE_LEN 9
#define HH_BYTES  3
#define MM_BYTES  3

#define LOGI(...) ESP_LOGI("IND", __VA_ARGS__)

static struct itask_struct {
    void (*setled)(bool state);
    int (*spi_tx)(uint8_t *pbuf, size_t len);
    bool is_initialized;
    bool is_synchronized;
} istruct = {.is_initialized = false, .is_synchronized = false};

static int nixie_convert(uint8_t *pbuf, uint8_t val)
{
    uint8_t result[3] = {0};
    if ((val > 99) || !pbuf) return -1;

    uint8_t ur = val / 10;
    if (ur == 0)
        result[1] = 1 << 4;
    else if (ur < 7)
        result[0] = 1 << ur;
    else
        result[1] = 1 << (ur - 6);

    ur = val % 10;
    if (ur == 0)
        result[2] = 1 << 7;
    else if (ur < 4)
        result[1] |= 1 << (ur + 4);
    else
        result[2] = 1 << (ur - 3);

    for (int i = 0; i < sizeof(result); ++i) *pbuf++ = result[i];

    return 0;
}

static int nixie_set_dots(uint8_t *pbuf, bool enable)
{
    if (pbuf == NULL) return -1;
    if (enable) {
        for (int i = 3; i < NIXIE_LEN; i += 3) *(pbuf + i) |= 0x80;
    }
    else {
        for (int i = 3; i < NIXIE_LEN; i += 3) *(pbuf + i) &= ~0x80;
    }
    return 0;
}

static int nixie_set_backlight(uint8_t *pbuf, bool enable)
{
    if (pbuf == NULL) return -1;
    if (enable) {
        for (int i = 0; i < NIXIE_LEN; i++) *(pbuf + i) |= 0x01;
    }
    else {
        for (int i = 0; i < NIXIE_LEN; i++) *(pbuf + i) &= ~0x01;
    }
    return 0;
}

static int nixie_indicate(struct timeval *tv, bool dots, bool backlit)
{
    int retval                    = 0;
    static uint8_t buf[NIXIE_LEN] = {0};

    if (tv == NULL) {
        for (int i = 0; i < NIXIE_LEN; ++i) 
            buf[i] = 0;    
    }
    else {
        struct tm timeinfo;
        localtime_r(&tv->tv_sec, &timeinfo);
        nixie_convert(buf, (uint8_t)timeinfo.tm_hour);
        nixie_convert(buf + HH_BYTES, (uint8_t)timeinfo.tm_min);
        nixie_convert(buf + HH_BYTES + MM_BYTES, (uint8_t)timeinfo.tm_sec);
    }

    if (dots) nixie_set_dots(buf, true);
    if (backlit) nixie_set_backlight(buf, true);

    istruct.spi_tx(buf, NIXIE_LEN);
    return retval;
}

void indication_set_led(bool state) {istruct.setled(state);}

void indication_set_initialized(bool isinit) { istruct.is_initialized = isinit; }

void indication_set_synchronized(bool issync) 
{ 
    istruct.is_synchronized = issync;
    if(issync)
        istruct.is_initialized = true;
}

static void task(void *p)
{
    struct timeval tv;
    struct timezone tz;
    time_t prev_sec = 0;
    LOGI("Starting indication");
    for (;;) {
    
        if (!istruct.is_initialized)
            nixie_indicate(NULL, false, true);
        else {
            gettimeofday(&tv, &tz);
            if(tv.tv_sec != prev_sec) {
                nixie_indicate(&tv, istruct.is_synchronized, false);
                prev_sec = tv.tv_sec;
            }
        }
        vTaskDelay(10);
     }
}

int indication_init(int (*spitx)(uint8_t *, size_t), void (*setled)(bool))
{
    if (spitx == NULL || setled == NULL) return -1;
    istruct.spi_tx = spitx;
    istruct.setled = setled;
    if (xTaskCreate(task, "IND", configMINIMAL_STACK_SIZE + 1024, NULL, IND_TASK_PRIORITY, NULL)) return 0;
    return -2;
}
