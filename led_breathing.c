/*********************************************************************
 * LED Breathing Effect - Pseudo-breathing via stepped duty cycle
 *
 * Hardware PWM can't reach LED pin (P0_1) on this board. Instead,
 * cycle through duty-cycle steps using HalLedBlink every 200ms to
 * create a visible fade-in/fade-out envelope. Low CPU overhead.
 *********************************************************************/

#include "led_breathing.h"
#include "hal_board_cfg.h"
#include "hal_led.h"
#include "OSAL.h"
#include "Debug.h"

#define LED_BREATHING_STEP_MS  200

/* Duty cycle envelope: ramp up then ramp down (percentage on-time) */
static const uint8 breathe_duty[] = {5, 15, 30, 50, 70, 90, 70, 50, 30, 15};
#define BREATHE_STEPS (sizeof(breathe_duty) / sizeof(breathe_duty[0]))

static uint8 led_breathing_task_id = 0;
static bool led_breathing_active = false;
static uint8 breathe_index = 0;

void led_breathing_init(uint8 task_id) {
    led_breathing_task_id = task_id;
}

void led_breathing_start(void) {
    if (led_breathing_active) {
        return;
    }
    led_breathing_active = true;
    breathe_index = 0;
    HalLedBlink(HAL_LED_1, 1, breathe_duty[0], LED_BREATHING_STEP_MS);
    osal_start_timerEx(led_breathing_task_id, LED_BREATHING_EVT, LED_BREATHING_STEP_MS);
}

void led_breathing_stop(void) {
    if (!led_breathing_active) {
        return;
    }
    led_breathing_active = false;
    osal_stop_timerEx(led_breathing_task_id, LED_BREATHING_EVT);
    HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
}

bool led_breathing_is_active(void) {
    return led_breathing_active;
}

uint16 led_breathing_event_loop(uint8 task_id, uint16 events) {
    if (events & LED_BREATHING_EVT) {
        if (led_breathing_active) {
            breathe_index = (breathe_index + 1) % BREATHE_STEPS;
            HalLedBlink(HAL_LED_1, 1, breathe_duty[breathe_index], LED_BREATHING_STEP_MS);
            osal_start_timerEx(led_breathing_task_id, LED_BREATHING_EVT, LED_BREATHING_STEP_MS);
        }
        return (events ^ LED_BREATHING_EVT);
    }
    return 0;
}
