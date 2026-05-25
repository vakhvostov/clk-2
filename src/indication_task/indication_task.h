#ifndef ITASK_H_
#define ITASK_H_

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

#define IND_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)

typedef enum {
    IND_COLOR_RED   = 0x55,  /* RED1-4 on  (bits 0,2,4,6) */
    IND_COLOR_GREEN = 0xAA,  /* GRN1-4 on  (bits 1,3,5,7) */
} ind_color_t;

typedef struct {
    uint8_t min_pct;   /* minimum brightness % (1-100) */
    uint8_t max_pct;   /* maximum brightness % (1-100) */
    float   gamma;     /* perceptual curve exponent, 2.2 recommended */
    uint8_t iir_shift; /* IIR smoothing: tau ~ 2^shift * 10ms (1-8, default 5 = ~320ms) */
} brightness_cfg_t;

void                    indication_set_color(ind_color_t color);
void                    indication_set_brightness_config(const brightness_cfg_t *cfg);
uint8_t                 indication_get_brightness_pct(void);
const brightness_cfg_t *indication_get_brightness_cfg(void);
int                     indication_init(int (*spitx)(uint8_t *, size_t));

#endif
