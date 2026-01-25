/*********************************************************************
 * LED Breathing Effect - Smooth slow pulse for pairing mode
 *
 * Uses HAL's built-in blink function with 70% duty cycle and 1000ms
 * period to create a smooth pulsing effect without strobe/flicker.
 * Battery efficient and visually smooth.
 *********************************************************************/

#include "led_breathing.h"
#include "hal_board_cfg.h"
#include "hal_led.h"
#include "OSAL.h"
#include "Debug.h"

// Breathing parameters (optimized for smooth visual effect and battery life)
#define LED_BREATHING_UPDATE_INTERVAL 100  // Update every 100ms for smooth pulsing
#define LED_BREATHING_FADE_STEPS 30        // 30 steps for smooth fade in/out (3 seconds per cycle)

static uint8 led_breathing_task_id = 0;
static bool led_breathing_active = false;

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

    LREP("LED breathing: START (smooth slow pulse)\r\n");

    // Use HAL's built-in blink function for smooth pulsing
    // Continuous slow pulse: 70% duty cycle, 1000ms period = smooth breathing effect
    HalLedBlink(HAL_LED_1, 0, 70, 1000);
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

    // Stop blinking and turn off LED
    HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);

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
 * @brief   Handle LED breathing timer events (now using HAL blink, minimal processing)
 */
uint16 led_breathing_event_loop(uint8 task_id, uint16 events) {
    if (events & LED_BREATHING_EVT) {
        // Event no longer used - breathing handled by HAL blink function
        return (events ^ LED_BREATHING_EVT);
    }

    return 0;
}
