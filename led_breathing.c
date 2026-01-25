/*********************************************************************
 * LED Breathing Effect - Aqara-style smooth fade for pairing mode
 *
 * Implements software-based breathing effect using OSAL timer.
 * Uses stepped brightness levels instead of full PWM for efficiency.
 *********************************************************************/

#include "led_breathing.h"
#include "hal_board_cfg.h"
#include "OSAL.h"
#include "Debug.h"

// Breathing parameters (optimized for battery life)
#define LED_BREATHING_UPDATE_INTERVAL 50  // Update every 50ms (20Hz)
#define LED_BREATHING_STEPS 20            // 20 brightness levels
#define LED_ON_TIME_PER_STEP 2            // ms LED is on per brightness level

static uint8 led_breathing_task_id = 0;
static bool led_breathing_active = false;
static uint8 led_brightness_step = 0;    // 0-19 (20 steps)
static int8 led_direction = 1;            // 1 = fade in, -1 = fade out

/*********************************************************************
 * @fn      led_breathing_init
 * @brief   Initialize LED breathing module
 */
void led_breathing_init(uint8 task_id) {
    led_breathing_task_id = task_id;
    LREP("LED breathing module initialized\r\n");
}

/*********************************************************************
 * @fn      led_breathing_start
 * @brief   Start LED breathing effect
 */
void led_breathing_start(void) {
    if (led_breathing_active) {
        LREP("LED breathing already active\r\n");
        return;
    }

    led_breathing_active = true;
    led_brightness_step = 0;
    led_direction = 1;

    LREP("LED breathing: START (Aqara-style smooth fade)\r\n");

    // Start breathing timer
    osal_start_reload_timer(led_breathing_task_id, LED_BREATHING_EVT, LED_BREATHING_UPDATE_INTERVAL);
}

/*********************************************************************
 * @fn      led_breathing_stop
 * @brief   Stop LED breathing effect and turn off LED
 */
void led_breathing_stop(void) {
    if (!led_breathing_active) {
        return;
    }

    led_breathing_active = false;
    osal_stop_timerEx(led_breathing_task_id, LED_BREATHING_EVT);

    // Ensure LED is off
    HAL_TURN_OFF_LED1();

    LREP("LED breathing: STOP\r\n");
}

/*********************************************************************
 * @fn      led_breathing_is_active
 * @brief   Check if breathing effect is currently active
 */
bool led_breathing_is_active(void) {
    return led_breathing_active;
}

/*********************************************************************
 * @fn      led_breathing_event_loop
 * @brief   Handle LED breathing timer events
 */
uint16 led_breathing_event_loop(uint8 task_id, uint16 events) {
    if (events & LED_BREATHING_EVT) {
        if (!led_breathing_active) {
            return (events ^ LED_BREATHING_EVT);
        }

        // Update brightness step
        led_brightness_step += led_direction;

        // Reverse direction at boundaries
        if (led_brightness_step >= LED_BREATHING_STEPS - 1) {
            led_brightness_step = LED_BREATHING_STEPS - 1;
            led_direction = -1;
        } else if (led_brightness_step <= 0) {
            led_brightness_step = 0;
            led_direction = 1;
        }

        // Simple brightness control: turn LED on for duration proportional to brightness
        // Step 0 = off, Step 19 = almost always on
        if (led_brightness_step > 0) {
            HAL_TURN_ON_LED1();

            // Keep LED on for time proportional to brightness
            // This creates perceived brightness levels without full PWM
            uint8 on_time_ms = led_brightness_step * LED_ON_TIME_PER_STEP;

            // Use blocking delay for simplicity (max 40ms, acceptable during pairing)
            volatile uint16 i;
            for (i = 0; i < on_time_ms * 100; i++) {
                asm("NOP");
            }

            HAL_TURN_OFF_LED1();
        }

        return (events ^ LED_BREATHING_EVT);
    }

    return 0;
}
