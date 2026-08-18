#ifndef ITASK_H_
#define ITASK_H_

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "stdbool.h"

#define IND_TASK_PRIORITY   (tskIDLE_PRIORITY + 2)

void indication_set_led(bool state);
void indication_set_synchronized(bool issinc);
void indication_set_initialized(bool isinit);
int indication_init(int (*spitx)(uint8_t *, size_t), void (*setled)(bool));



#endif
