#ifndef LED_BREATHING_H
#define LED_BREATHING_H

#include "hal_types.h"

// LED Breathing event for OSAL
#define LED_BREATHING_EVT 0x4000

// Function prototypes
extern void led_breathing_init(uint8 task_id);
extern void led_breathing_start(void);
extern void led_breathing_stop(void);
extern bool led_breathing_is_active(void);
extern uint16 led_breathing_event_loop(uint8 task_id, uint16 events);

#endif // LED_BREATHING_H
