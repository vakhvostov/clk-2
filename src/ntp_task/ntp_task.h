#ifndef NTPTASK_H_
#define NTPTASK_H_

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "stdbool.h"
#include "time.h"

#define NTP_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)
#define MAX_SERV_NAMELEN 31
#define NTP_SERV_NAMESIZE (MAX_SERV_NAMELEN + 1)
#define MAX_TZ_NAMELEN 31
#define NTP_TZ_NAMESIZE (MAX_TZ_NAMELEN + 1)
#define NTP_SERVER_CNT 3



int ntp_task_init();
void ntp_set_suspend(bool suspend);
void ntp_force_resync();
void ntp_reload_config();
time_t ntp_get_lastsync(void);
const char* ntp_get_ssid(void);
const char* ntp_get_servers(int);
bool ntp_get_sync_status(void);

#endif
