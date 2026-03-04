/*********************************************************************
 * LED Breathing Effect - Simple slow blink using HalLedSet + OSAL timer
 *
 * IMPORTANT: Do NOT use HalLedBlink() on battery SEDs!
 * HalLedBlink() starts an internal HAL task timer (HAL_LED_BLINK_EVENT)
 * that cannot be cancelled from application code. This causes:
 *   1. LED turning back ON after led_breathing_stop()
 *   2. Pending HAL timer preventing PM2/PM3 sleep entry
 *   3. Wasted battery from phantom wake-ups
 *
 * Instead, use HalLedSet() (direct GPIO) + our own OSAL timer.
 * Simple ON/OFF toggle every 500ms = visible, battery-friendly.
 *********************************************************************/

#include "led_breathing.h"
#include "hal_board_cfg.h"
#include "hal_led.h"
#include "OSAL.h"
#include "Debug.h"

/* Slow blink: 500ms ON, 500ms OFF = 1 Hz blink rate */
#define LED_BREATHING_STEP_MS  500

static uint8 led_breathing_task_id = 0;
static bool led_breathing_active = false;
static bool led_state_on = false;

void led_breathing_init(uint8 task_id) {
    led_breathing_task_id = task_id;
}

void led_breathing_start(void) {
    if (led_breathing_active) {
        return;
    }
    led_breathing_active = true;
    led_state_on = true;
    HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);
    osal_start_timerEx(led_breathing_task_id, LED_BREATHING_EVT, LED_BREATHING_STEP_MS);
}

void led_breathing_stop(void) {
    if (!led_breathing_active) {
        return;
    }
    led_breathing_active = false;
    led_state_on = false;
    osal_stop_timerEx(led_breathing_task_id, LED_BREATHING_EVT);
    HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
    /* No HAL internal timers left running — safe for PM2/PM3 entry */
}

bool led_breathing_is_active(void) {
    return led_breathing_active;
}

uint16 led_breathing_event_loop(uint8 task_id, uint16 events) {
    if (events & LED_BREATHING_EVT) {
        if (led_breathing_active) {
            /* Toggle LED using direct GPIO set — no HAL blink timers */
            led_state_on = !led_state_on;
            HalLedSet(HAL_LED_1, led_state_on ? HAL_LED_MODE_ON : HAL_LED_MODE_OFF);
            osal_start_timerEx(led_breathing_task_id, LED_BREATHING_EVT, LED_BREATHING_STEP_MS);
        }
        return (events ^ LED_BREATHING_EVT);
    }
    return 0;
}
