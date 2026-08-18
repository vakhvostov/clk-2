#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdatomic.h>
#include "cli_task.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "linenoise/linenoise.h"
#include "time.h"
#include "ntp_task.h"
#include "nvs_fx.h"


#define LOGI(...) ESP_LOGI("#", __VA_ARGS__)
const char *prompt = "clk>";

/* --------------------------------------------------------------------------
 * Settable-key descriptor table
 * -------------------------------------------------------------------------- */
typedef struct {
    const char *name;       /* name used on the CLI:  set <name> <val>  */
    const char *nvs_key;    /* key stored in NVS                        */
    const char *hint;       /* shown in usage / help                    */
} set_entry_t;

static const set_entry_t set_table[] = {
    { "tz",   "tz",        "<TZString>   POSIX timezone string (e.g. CET-1CEST,M3.5.0/2,M10.5.0/3)" },
    { "ssid", "wifi_ssid", "<SSID>       WiFi network name"                      },
    { "pass", "wifi_pass", "<Password>   WiFi password"                          },
    { "ntp0", "ntp0",      "<URL>        Primary NTP server"                    },
    { "ntp1", "ntp1",      "<URL>        Secondary NTP server"                  },
    { "ntp2", "ntp2",      "<URL>        Tertiary NTP server"                   },
};
#define SET_TABLE_LEN (sizeof(set_table) / sizeof(set_table[0]))

static atomic_bool s_console_active = false;

static int gated_log_vprintf(const char *fmt, va_list args)
{
    if (atomic_load(&s_console_active)) return 0;
    return vprintf(fmt, args);
}

static const set_entry_t *find_set_entry(const char *name)
{
    for (size_t i = 0; i < SET_TABLE_LEN; ++i)
        if (strcmp(set_table[i].name, name) == 0)
            return &set_table[i];
    return NULL;
}

/* --------------------------------------------------------------------------
 * Console peripheral / library init
 * -------------------------------------------------------------------------- */
static void init_console_peripheral(void)
{
    fflush(stdout);
    fsync(fileno(stdout));
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin),  F_SETFL, 0);
    usb_serial_jtag_driver_config_t jtag_config = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&jtag_config));
    usb_serial_jtag_vfs_use_driver();
}

static void init_console_lib(void)
{
    esp_console_config_t cliconf = {
        .max_cmdline_length = 255,
        .max_cmdline_args   = 9,
        .hint_bold          = 1,
    };
    ESP_ERROR_CHECK(esp_console_init(&cliconf));
}

/* --------------------------------------------------------------------------
 * Commands
 * -------------------------------------------------------------------------- */
static int cli_cmd_stat(int argc, char **argv)
{
    char *tz = _getenv_r(_REENT, "TZ");

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);

    printf("Clock status:\nTZ: %s\n", tz ? tz : "(not set)");
    printf("Current time: %02d-%02d-%04d %02d:%02d:%02d\n",
           timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
           timeinfo.tm_hour, timeinfo.tm_min,      timeinfo.tm_sec);

    tv.tv_sec = ntp_get_lastsync();
    if (tv.tv_sec == 0) {
        printf("Last sync:    never\n");
    } else {
        localtime_r(&tv.tv_sec, &timeinfo);
        printf("Last sync:    %02d-%02d-%04d %02d:%02d:%02d\n",
               timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
               timeinfo.tm_hour, timeinfo.tm_min,      timeinfo.tm_sec);
    }

    printf("Servers:\n");
    for (int i = 0; i < NTP_SERVER_CNT; ++i)
        printf("  %s\n", ntp_get_servers(i));

    printf("SSID: %s\n", ntp_get_ssid());
    return 0;
}

/* set <key> <value>
 * Looks up <key> in set_table, stores <value> in NVS, then reloads NTP config. */
static int cli_cmd_set(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: set <key> <value>\nAvailable keys:\n");
        for (size_t i = 0; i < SET_TABLE_LEN; ++i)
            printf("  %-8s  %s\n", set_table[i].name, set_table[i].hint);
        return -1;
    }

    const set_entry_t *entry = find_set_entry(argv[1]);
    if (!entry) {
        printf("Unknown key '%s'. Run 'set' without arguments to list valid keys.\n", argv[1]);
        return -1;
    }

    nvs_fx_set(entry->nvs_key, argv[2]);
    ntp_reload_config();
    printf("Saved. NTP resync triggered.\n");
    LOGI("set %s (nvs: %s) = %s", entry->name, entry->nvs_key, argv[2]);
    return 0;
}

static int cli_cmd_log(int argc, char **argv)
{
    char c = 0;
    while (c != '\x03') {
        if (read(STDIN_FILENO, &c, 1) <= 0)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    return 0;
}

static int cli_cmd_resync(int argc, char **argv)
{
    ntp_force_resync();
    printf("Resync triggered.\n");
    return 0;
}



/* --------------------------------------------------------------------------
 * Task
 * -------------------------------------------------------------------------- */
void cli_task(void *arg)
{
    esp_log_set_vprintf(gated_log_vprintf);
    init_console_peripheral();
    init_console_lib();

    const esp_console_cmd_t stat_def = {
        .command  = "stat",
        .help     = "Display SSID, timezone, current time and last NTP sync",
        .func     = cli_cmd_stat,
        .argtable = NULL,
    };

    const esp_console_cmd_t set_def = {
        .command  = "set",
        .hint     = "<key> <value>",
        .help     = "Store a configuration value (run 'set' alone to list keys)",
        .func     = cli_cmd_set,
        .argtable = NULL,
    };

    const esp_console_cmd_t log_def = {
        .command  = "log",
        .help     = "Enter log viewer (Ctrl-C to exit)",
        .func     = cli_cmd_log,
        .argtable = NULL,
    };

    const esp_console_cmd_t resync_def = {
        .command  = "resync",
        .help     = "Force NTP resynchronization",
        .func     = cli_cmd_resync,
        .argtable = NULL,
    };



    esp_console_cmd_register(&stat_def);
    esp_console_cmd_register(&set_def);
    esp_console_cmd_register(&log_def);
    esp_console_cmd_register(&resync_def);
    esp_console_register_help_command();
    LOGI("CLI init complete");

    for (;;) {
        atomic_store(&s_console_active, true);
        char *line = linenoise(prompt);
        atomic_store(&s_console_active, false);
        if (line == NULL) continue;

        int ret;
        esp_console_run(line, &ret);
        linenoiseFree(line);
    }
}

int cli_task_init(void)
{
    LOGI("CLI init registration");
    if (xTaskCreate(cli_task, "CLI-Task", configMINIMAL_STACK_SIZE + 4096,
                    NULL, CLI_TASK_PRIORITY, NULL))
        return 0;
    LOGI("CLI init registration failed");
    return -1;
}
