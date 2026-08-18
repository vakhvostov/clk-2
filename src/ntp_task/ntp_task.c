#include "ntp_task.h"

#include <esp_wifi.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "indication_task.h"
#include "nvs_fx.h"
#include "stdbool.h"
#include "sys/time.h"
#include "time.h"
#include "string.h"

#define LOGI(...) ESP_LOGI("NTP", __VA_ARGS__)
#define LOGE(...) ESP_LOGE("NTP", __VA_ARGS__)
#define LOGW(...) ESP_LOGW("NTP", __VA_ARGS__)

#define RENEW_PERIOD_SEC    7200
#define WIFI_CONNECT_TIMEOUT_MS 15000

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;

static struct ntp_struct {
    bool never_synchronised;
    /* 0 = no sync yet; i+1 = index of server that last succeeded */
    uint8_t last_sync_success;
    bool force_resync;
    bool suspend;
    struct timeval lasttv;
} nstruct = {.never_synchronised = true, .last_sync_success = 0,
             .force_resync = false, .suspend = false};

static wifi_config_t wifi_config = {.sta.ssid = "default_ssid"};
static char ntplist[NTP_SERVER_CNT][NTP_SERV_NAMESIZE];
static char tzone[NTP_TZ_NAMESIZE];
static char lastntp[NTP_SERV_NAMESIZE + 2]; /* server name + " X" + null */

static bool istimetorenew()
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return tv.tv_sec - nstruct.lasttv.tv_sec > RENEW_PERIOD_SEC;
}

static bool needs_sync()
{
    return nstruct.never_synchronised
           || nstruct.force_resync
           || istimetorenew();
}

void ntp_set_suspend(bool suspend)
{
    nstruct.suspend = suspend;
}

void ntp_force_resync()
{
    nstruct.suspend = false;
    nstruct.force_resync = true;
}

void ntp_reload_config()
{
    nvs_fx_get("wifi_ssid", (char *)wifi_config.sta.ssid,     sizeof(wifi_config.sta.ssid));
    nvs_fx_get("wifi_pass", (char *)wifi_config.sta.password,  sizeof(wifi_config.sta.password));
    nvs_fx_get("ntp0",      ntplist[0], NTP_SERV_NAMESIZE);
    nvs_fx_get("ntp1",      ntplist[1], NTP_SERV_NAMESIZE);
    nvs_fx_get("ntp2",      ntplist[2], NTP_SERV_NAMESIZE);
    nvs_fx_get("tz",        tzone,      NTP_TZ_NAMESIZE);
    _setenv_r(_REENT, "TZ", tzone, 1);
    tzset();
    if (strcmp((char *)wifi_config.sta.ssid, "default_ssid") != 0)
        nstruct.suspend = false;
    nstruct.force_resync = true;
    LOGI("Config reloaded");
}

time_t ntp_get_lastsync(void)
{
    return nstruct.lasttv.tv_sec;
}

const char* ntp_get_ssid(void)
{
    return (const char*)wifi_config.sta.ssid;
}

const char* ntp_get_servers(int num)
{
    if (num >= NTP_SERVER_CNT)
        return NULL;
    if (nstruct.last_sync_success > 0 && (uint8_t)(nstruct.last_sync_success - 1) == (uint8_t)num) {
        strncpy(lastntp, ntplist[num], sizeof(lastntp) - 3);
        lastntp[sizeof(lastntp) - 3] = '\0';
        strcat(lastntp, " X");
        return (const char*)lastntp;
    }
    return (const char*)(ntplist[num]);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
}

static bool wifi_start_and_connect(void)
{
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);

    if (ESP_OK != esp_wifi_start()) {
        LOGE("WiFi start failed");
        return false;
    }
    if (ESP_OK != esp_wifi_connect()) {
        LOGE("WiFi connect failed");
        esp_wifi_stop();
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & WIFI_CONNECTED_BIT)
        return true;

    LOGE("WiFi connection timed out or failed");
    esp_wifi_stop();
    return false;
}

static int8_t get_wifi_rssi() {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        return ap_info.rssi;
    else
        return 0;
}

static void wifi_stop_and_disconnect(void)
{
    LOGI("WiFi RSSI: %d", get_wifi_rssi());
    esp_wifi_disconnect();
    esp_wifi_stop();
}

static bool try_ntp_sync(const esp_sntp_config_t *sntpcfg)
{
    if (ESP_OK != esp_netif_sntp_init(sntpcfg)) {
        LOGE("SNTP init failed");
        return false;
    }
    bool synced = (ESP_OK == esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000)));
    esp_netif_sntp_deinit();
    return synced;
}

static void init_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    LOGI("WiFi initialized");
}

static void task(void *p)
{
    LOGI("NTP task started");

    nvs_fx_get("wifi_ssid", (char *)wifi_config.sta.ssid,     sizeof(wifi_config.sta.ssid));
    nvs_fx_get("wifi_pass", (char *)wifi_config.sta.password,  sizeof(wifi_config.sta.password));
    nvs_fx_get("ntp0",      ntplist[0], NTP_SERV_NAMESIZE);
    nvs_fx_get("ntp1",      ntplist[1], NTP_SERV_NAMESIZE);
    nvs_fx_get("ntp2",      ntplist[2], NTP_SERV_NAMESIZE);
    nvs_fx_get("tz",        tzone,      NTP_TZ_NAMESIZE);

    _setenv_r(_REENT, "TZ", tzone, 1);
    tzset();

    init_wifi();

    if (strcmp((char *)wifi_config.sta.ssid, "default_ssid") == 0) {
        LOGW("Default SSID detected, suspending NTP sync");
        nstruct.suspend = true;
    }

    for (;;) {
        if (nstruct.suspend) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (needs_sync()) {
            LOGI("Attempting NTP sync");
            nstruct.last_sync_success = 0;

            if (wifi_start_and_connect()) {
                for (int i = 0; i < NTP_SERVER_CNT; i++) {
                    if (ntplist[i][0] == 0)
                        continue;
                    esp_sntp_config_t sntpcfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntplist[i]);
                    bool synced = try_ntp_sync(&sntpcfg);
                    if (synced) {
                        gettimeofday(&nstruct.lasttv, NULL);
                        nstruct.never_synchronised = false;
                        nstruct.force_resync       = false;
                        nstruct.last_sync_success  = i + 1;
                        LOGI("NTP sync successful");
                        break;
                    } else if (i == NTP_SERVER_CNT - 1) {
                        LOGW("NTP sync failed on all servers");
                    }
                }
                wifi_stop_and_disconnect();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int ntp_task_init()
{
    if (xTaskCreate(task, "NTP", configMINIMAL_STACK_SIZE + 8192, NULL, NTP_TASK_PRIORITY, NULL))
        return 0;
    return -2;
}

bool ntp_get_sync_status(void)
{
    return !needs_sync();
}
