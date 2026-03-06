#include "factory_reset.h"
#include "AF.h"
#include "Debug.h"
#include "OnBoard.h"
#include "bdb.h"
#include "bdb_interface.h"
#include "hal_led.h"
#include "led_breathing.h"
#include "ZComDef.h"
#include "hal_key.h"
#include "commissioning.h"

static void zclFactoryResetter_ResetToFN(void);
static void zclFactoryResetter_ProcessBootCounter(void);
static void zclFactoryResetter_ResetBootCounter(void);

static uint8 zclFactoryResetter_TaskID;
bool zclFactoryResetter_WarningActive = false;
static bool breathing_was_active_before_warning = false;

uint16 zclFactoryResetter_loop(uint8 task_id, uint16 events) {
    LREP("zclFactoryResetter_loop 0x%X\r\n", events);

    if (events & FACTORY_RESET_HOLD_WARNING_EVT) {
        // User has held button long enough — show solid LED as "factory reset imminent" warning
        LREPMaster("FACTORY_RESET_HOLD_WARNING: Starting LED feedback\r\n");
        zclFactoryResetter_WarningActive = true;
        breathing_was_active_before_warning = led_breathing_is_active();
        if (breathing_was_active_before_warning) {
            led_breathing_stop();  // Stop pairing blink so solid ON is visible
        }
        HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);  // Solid ON warning (avoid HalLedBlink on SED)
        return (events ^ FACTORY_RESET_HOLD_WARNING_EVT);
    }

    if (events & FACTORY_RESET_EVT) {
        LREPMaster("FACTORY_RESET_EVT\r\n");
        zclFactoryResetter_ResetToFN();
        return (events ^ FACTORY_RESET_EVT);
    }

    if (events & FACTORY_BOOTCOUNTER_RESET_EVT) {
        LREPMaster("FACTORY_BOOTCOUNTER_RESET_EVT\r\n");
        zclFactoryResetter_ResetBootCounter();
        return (events ^ FACTORY_BOOTCOUNTER_RESET_EVT);
    }
    return 0;
}
void zclFactoryResetter_ResetBootCounter(void) {
    uint16 bootCnt = 0;
    LREPMaster("Clear boot counter\r\n");
    osal_nv_write(ZCD_NV_BOOTCOUNTER, 0, sizeof(bootCnt), &bootCnt);
}

void zclFactoryResetter_Init(uint8 task_id) {
    zclFactoryResetter_TaskID = task_id;
    /**
     * We can't register more than one task, call in main app taks
     * zclFactoryResetter_HandleKeys(portAndAction, keyCode);
     * */
    // RegisterForKeys(task_id);
#if FACTORY_RESET_BY_BOOT_COUNTER
    zclFactoryResetter_ProcessBootCounter();
#endif
}

void zclFactoryResetter_ResetToFN(void) {
    // LED already blinking from warning event - will continue until device resets
    // After reset, commissioning.c will control LED for pairing mode (fast blinks)
    LREP("bdbAttributes.bdbNodeIsOnANetwork=%d bdbAttributes.bdbCommissioningMode=0x%X\r\n", bdbAttributes.bdbNodeIsOnANetwork, bdbAttributes.bdbCommissioningMode);
    LREPMaster("zclFactoryResetter: Reset to FN\r\n");
    // Clear commissioning backoff/metrics so device immediately rejoins after reset
    zclCommissioning_ResetState();
    bdb_resetLocalAction();
}

void zclFactoryResetter_HandleKeys(uint8 portAndAction, uint8 keyCode) {
#if FACTORY_RESET_BY_LONG_PRESS
    if (portAndAction & HAL_KEY_RELEASE) {
        LREPMaster("zclFactoryResetter: Key release\r\n");
        osal_stop_timerEx(zclFactoryResetter_TaskID, FACTORY_RESET_EVT);
        osal_stop_timerEx(zclFactoryResetter_TaskID, FACTORY_RESET_HOLD_WARNING_EVT);
        if (zclFactoryResetter_WarningActive) {
            HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
            zclFactoryResetter_WarningActive = false;
            // Restore pairing LED if it was running before the warning interrupted it
            // (user released between warning and factory reset — pairing continues)
            if (breathing_was_active_before_warning) {
                led_breathing_start();
                breathing_was_active_before_warning = false;
            }
        }
    } else {
        LREPMaster("zclFactoryResetter: Key press\r\n");
        bool statTimer = true;
#if FACTORY_RESET_BY_LONG_PRESS_PORT
        statTimer = FACTORY_RESET_BY_LONG_PRESS_PORT & portAndAction;
#endif
        LREP("zclFactoryResetter statTimer hold timer %d\r\n", statTimer);
        if (statTimer) {
            uint32 timeout = bdbAttributes.bdbNodeIsOnANetwork ? FACTORY_RESET_HOLD_TIME_LONG : FACTORY_RESET_HOLD_TIME_FAST;

            // Start visual warning 3 seconds before factory reset fires
            // (e.g., at 7s for a 10s reset). Solid LED ON tells user:
            // "keep holding 3 more seconds for factory reset, or release to cancel"
            uint32 warningDelay = (timeout > 3000) ? (timeout - 3000) : 1000;
            osal_start_timerEx(zclFactoryResetter_TaskID, FACTORY_RESET_HOLD_WARNING_EVT, warningDelay);

            // Start actual factory reset timer
            osal_start_timerEx(zclFactoryResetter_TaskID, FACTORY_RESET_EVT, timeout);
        }
    }
#endif
}

void zclFactoryResetter_ProcessBootCounter(void) {
    LREPMaster("zclFactoryResetter_ProcessBootCounter\r\n");
    osal_start_timerEx(zclFactoryResetter_TaskID, FACTORY_BOOTCOUNTER_RESET_EVT, FACTORY_RESET_BOOTCOUNTER_RESET_TIME);

    uint16 bootCnt = 0;
    if (osal_nv_item_init(ZCD_NV_BOOTCOUNTER, sizeof(bootCnt), &bootCnt) == ZSUCCESS) {
        osal_nv_read(ZCD_NV_BOOTCOUNTER, 0, sizeof(bootCnt), &bootCnt);
    }
    LREP("bootCnt %d\r\n", bootCnt);
    bootCnt += 1;
    if (bootCnt >= FACTORY_RESET_BOOTCOUNTER_MAX_VALUE) {
        LREP("bootCnt =%d greater than, ressetting %d\r\n", bootCnt, FACTORY_RESET_BOOTCOUNTER_MAX_VALUE);
        bootCnt = 0;
        osal_stop_timerEx(zclFactoryResetter_TaskID, FACTORY_BOOTCOUNTER_RESET_EVT);
        osal_start_timerEx(zclFactoryResetter_TaskID, FACTORY_RESET_EVT, 5000);
    }
    osal_nv_write(ZCD_NV_BOOTCOUNTER, 0, sizeof(bootCnt), &bootCnt);
}
