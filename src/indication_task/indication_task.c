#include "indication_task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "board_config.h"
#include "sys/time.h"
#include "time.h"
#include "math.h"
#include "stdlib.h"
#include "stdio.h"
#include "nvs_fx.h"

#define LOGI(...) ESP_LOGI("IND", __VA_ARGS__)

/* -------------------------------------------------------------------------
 * PWM / brightness
 * ------------------------------------------------------------------------- */
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_FREQ_HZ    10000
#define LEDC_RES        LEDC_TIMER_12_BIT
#define LEDC_MAX_DUTY   4095u   /* 2^12 - 1 */

static uint16_t s_lut[256];     /* ADC[11:4] -> /OE PWM duty */

static const brightness_cfg_t s_default_brightness = {
    .min_pct   = 5,
    .max_pct   = 100,
    .gamma     = 2.2f,
    .iir_shift = 5,   /* tau ~320 ms at 10 ms sample rate */
};

/*
 * Build LUT mapping 8-bit ADC index (0-255) to 12-bit /OE duty.
 * /OE is active-low: duty 0 = max brightness, duty 4095 = off.
 *
 * Curve: t^(1/gamma) bows the response upward so equal ADC steps
 * produce equal perceived brightness steps.
 */
static void build_lut(const brightness_cfg_t *cfg)
{
    float bmin     = cfg->min_pct / 100.0f;
    float brange   = (cfg->max_pct - cfg->min_pct) / 100.0f;
    float inv_gamma = 1.0f / cfg->gamma;

    for (int i = 0; i < 256; i++) {
        float t          = i / 255.0f;
        float brightness = bmin + powf(t, inv_gamma) * brange;
        s_lut[i] = (uint16_t)((1.0f - brightness) * LEDC_MAX_DUTY + 0.5f);
    }
}

static void ledc_pwm_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = OE_EN_PIN,
        .duty       = s_lut[0],  /* start at minimum brightness */
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

static brightness_cfg_t s_brightness_cfg;
static uint8_t          s_current_pct = 0;

/* IIR state: accumulator holds filtered_value * 2^shift */
static int32_t s_iir_acc   = 0;
static uint8_t s_iir_shift = 5;

static int iir_filter(int sample)
{
    s_iir_acc += sample - (s_iir_acc >> s_iir_shift);
    return s_iir_acc >> s_iir_shift;
}

static void load_brightness_from_nvs(brightness_cfg_t *cfg)
{
    char buf[16];

    buf[0] = '\0';
    nvs_fx_get("br_min", buf, sizeof(buf));
    if (buf[0]) cfg->min_pct = (uint8_t)atoi(buf);

    buf[0] = '\0';
    nvs_fx_get("br_max", buf, sizeof(buf));
    if (buf[0]) cfg->max_pct = (uint8_t)atoi(buf);

    buf[0] = '\0';
    nvs_fx_get("br_gamma", buf, sizeof(buf));
    if (buf[0]) cfg->gamma = strtof(buf, NULL);

    buf[0] = '\0';
    nvs_fx_get("br_iir", buf, sizeof(buf));
    if (buf[0]) cfg->iir_shift = (uint8_t)atoi(buf);
}

static void save_brightness_to_nvs(const brightness_cfg_t *cfg)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d",   cfg->min_pct);   nvs_fx_set("br_min",   buf);
    snprintf(buf, sizeof(buf), "%d",   cfg->max_pct);   nvs_fx_set("br_max",   buf);
    snprintf(buf, sizeof(buf), "%.2f", cfg->gamma);     nvs_fx_set("br_gamma", buf);
    snprintf(buf, sizeof(buf), "%d",   cfg->iir_shift); nvs_fx_set("br_iir",   buf);
}

void indication_set_brightness_config(const brightness_cfg_t *cfg)
{
    if (cfg->iir_shift != s_iir_shift) {
        /* Rescale accumulator so the output value is preserved */
        int current = s_iir_acc >> s_iir_shift;
        s_iir_shift  = cfg->iir_shift;
        s_iir_acc    = current << s_iir_shift;
    }
    s_brightness_cfg = *cfg;
    build_lut(cfg);
    save_brightness_to_nvs(cfg);
}

uint8_t indication_get_brightness_pct(void)
{
    return s_current_pct;
}

const brightness_cfg_t *indication_get_brightness_cfg(void)
{
    return &s_brightness_cfg;
}

/* -------------------------------------------------------------------------
 * ADC
 * ------------------------------------------------------------------------- */
static adc_oneshot_unit_handle_t s_adc_handle;

static void init_adc(void)
{
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(s_adc_handle, (adc_channel_t)(ADC_IN_PIN - 1), &chan_cfg);
}

static int read_adc(void)
{
    int raw = 0;
    adc_oneshot_read(s_adc_handle, (adc_channel_t)(ADC_IN_PIN - 1), &raw);
    return raw;
}

/* -------------------------------------------------------------------------
 * Display frame builder
 * -------------------------------------------------------------------------
 * SPI chain: MCU -> U2 -> U3 -> U4 -> U5  (64 bits / 8 bytes, MSB first)
 *
 * buf[0..1] -> U5 : digit 1 (hours tens),   digit 2 (hours ones)
 * buf[2..3] -> U4 : digit 3 (minutes tens), digit 4 (minutes ones)
 * buf[4..5] -> U3 : digit 5 (seconds tens), digit 6 (seconds ones)
 * buf[6]    -> U2 high : digit 7 (ms hundreds)
 * buf[7]    -> U2 low  : color LEDs (RED1-4, GRN1-4)
 *
 * Wire-byte bit order (from schematic):
 *   Odd digits  (1,3,5,7) -> bits[7:0] = B,A,F,G,E,D,C,DP
 *   Even digits (2,4,6)   -> bits[7:0] = E,D,C,DP,B,A,F,G
 */

/* Standard seg byte: bit0=A, bit1=B, bit2=C, bit3=D, bit4=E, bit5=F, bit6=G, bit7=DP */
static const uint8_t seg7[10] = {
    0x3F, /* 0: ABCDEF  */
    0x06, /* 1: BC      */
    0x5B, /* 2: ABDEG   */
    0x4F, /* 3: ABCDG   */
    0x66, /* 4: BCFG    */
    0x6D, /* 5: ACDFG   */
    0x7D, /* 6: ACDEFG  */
    0x07, /* 7: ABC     */
    0x7F, /* 8: ABCDEFG */
    0x6F, /* 9: ABCDFG  */
};

static uint8_t encode_odd(uint8_t s)   /* digits 1, 3, 5, 7 */
{
    return ((s >> 1) & 1) << 7  /* B  */
         | ((s >> 0) & 1) << 6  /* A  */
         | ((s >> 5) & 1) << 5  /* F  */
         | ((s >> 6) & 1) << 4  /* G  */
         | ((s >> 4) & 1) << 3  /* E  */
         | ((s >> 3) & 1) << 2  /* D  */
         | ((s >> 2) & 1) << 1  /* C  */
         | ((s >> 7) & 1) << 0; /* DP */
}

static uint8_t encode_even(uint8_t s)  /* digits 2, 4, 6 */
{
    return ((s >> 4) & 1) << 7  /* E  */
         | ((s >> 3) & 1) << 6  /* D  */
         | ((s >> 2) & 1) << 5  /* C  */
         | ((s >> 7) & 1) << 4  /* DP */
         | ((s >> 1) & 1) << 3  /* B  */
         | ((s >> 0) & 1) << 2  /* A  */
         | ((s >> 5) & 1) << 1  /* F  */
         | ((s >> 6) & 1) << 0; /* G  */
}

static void build_frame(uint8_t buf[8], int h, int m, int s, int ms100, uint8_t color)
{
    buf[0] = encode_odd (seg7[h   / 10]); /* D1 hours tens    */
    buf[1] = encode_even(seg7[h   % 10]); /* D2 hours ones    */
    buf[2] = encode_odd (seg7[m   / 10]); /* D3 minutes tens  */
    buf[3] = encode_even(seg7[m   % 10]); /* D4 minutes ones  */
    buf[4] = encode_odd (seg7[s   / 10]); /* D5 seconds tens  */
    buf[5] = encode_even(seg7[s   % 10]); /* D6 seconds ones  */
    buf[6] = encode_odd (seg7[ms100]);    /* D7 ms hundreds   */
    buf[7] = color;
}

/* -------------------------------------------------------------------------
 * Task
 * ------------------------------------------------------------------------- */
static struct {
    int (*spi_tx)(uint8_t *, size_t);
    uint8_t color;
} istruct = { .color = IND_COLOR_GREEN };

void indication_set_color(ind_color_t color)
{
    istruct.color = (uint8_t)color;
}

static void task(void *p)
{
    struct timeval tv;
    struct tm      timeinfo;
    uint8_t        buf[8];
    LOGI("Indication task started");

    for (;;) {
        /* Brightness: ADC -> IIR -> LUT[8-bit index] -> /OE PWM duty */
        uint16_t duty = s_lut[iir_filter(read_adc()) >> 4];
        s_current_pct = (uint8_t)((uint32_t)(LEDC_MAX_DUTY - duty) * 100u / LEDC_MAX_DUTY);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &timeinfo);
        int ms100 = (int)(tv.tv_usec / 100000);

        build_frame(buf,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                    ms100, istruct.color);
        istruct.spi_tx(buf, sizeof(buf));

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* -------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */
int indication_init(int (*spitx)(uint8_t *, size_t))
{
    if (spitx == NULL) return -1;
    istruct.spi_tx = spitx;

    s_brightness_cfg = s_default_brightness;
    load_brightness_from_nvs(&s_brightness_cfg);  /* override defaults with stored values */
    s_iir_shift = s_brightness_cfg.iir_shift;
    init_adc();
    build_lut(&s_brightness_cfg);
    ledc_pwm_init();

    if (xTaskCreate(task, "IND", configMINIMAL_STACK_SIZE + 2048,
                    NULL, IND_TASK_PRIORITY, NULL))
        return 0;
    return -2;
}
