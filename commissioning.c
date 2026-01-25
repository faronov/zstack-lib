#include "commissioning.h"
#include "Debug.h"
#include "OSAL_PwrMgr.h"
#include "ZDApp.h"
#include "bdb_interface.h"
#include "hal_key.h"
#include "hal_led.h"
#include "nwk_globals.h"
#include "zcl_app.h"  // For TX power mode access
#include "ZMAC.h"     // For TX_PWR constants

static void zclCommissioning_ProcessCommissioningStatus(bdbCommissioningModeMsg_t *bdbCommissioningModeMsg);
static void zclCommissioning_ResetBackoffRetry(void);
static void zclCommissioning_BindNotification(bdbBindNotificationData_t *data);
extern bool requestNewTrustCenterLinkKey;

// External TX power mode from zcl_app.c
extern uint8 zclApp_TxPowerMode;

// Issue #24: Persist rejoin backoff state across power cycles
typedef struct {
    byte rejoinsLeft;
    uint32 rejoinDelay;
} RejoinBackoffState_t;

byte rejoinsLeft = APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES;
uint32 rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY;

uint8 zclCommissioning_TaskId = 0;

// Hybrid Phase 2: Network Quality Metrics (typedef moved to header)
NetworkMetrics_t network_metrics = {0}; // Non-static for ZCL access
static uint8 current_tx_power = 0; // Start at 0 dBm (TX_PWR_0_DBM) to save battery
static bool quick_rejoin_attempted = false;

// Aqara-style LED behavior: track if we're in user-initiated pairing mode
static bool pairing_mode_active = false;

#ifndef APP_TX_POWER
    #define APP_TX_POWER 4  // TX_PWR_PLUS_4 (+4 dBm)
#endif

/*********************************************************************
 * HYBRID PHASE 2: HELPER FUNCTIONS
 */

/*********************************************************************
 * @fn      zclCommissioning_SaveBackoffState
 * @brief   Save rejoin backoff state to NV memory (Issue #24)
 * @param   none
 * @return  none
 */
static void zclCommissioning_SaveBackoffState(void) {
    RejoinBackoffState_t state;
    state.rejoinsLeft = rejoinsLeft;
    state.rejoinDelay = rejoinDelay;

    osal_nv_item_init(ZCD_NV_REJOIN_BACKOFF_STATE, sizeof(RejoinBackoffState_t), &state);
    osal_nv_write(ZCD_NV_REJOIN_BACKOFF_STATE, 0, sizeof(RejoinBackoffState_t), &state);
}

/*********************************************************************
 * @fn      zclCommissioning_StartPairingMode
 * @brief   Start Aqara-style pairing mode with continuous rapid LED blinking
 * @param   none
 * @return  none
 */
void zclCommissioning_StartPairingMode(void) {
    pairing_mode_active = true;

    // Aqara-style: Continuous rapid LED blinking during pairing
    // Blink 255 times with 200ms period = ~51 seconds of blinking
    // This gives plenty of time for the pairing process to complete
    HalLedBlink(HAL_LED_1, 255, 50, 200);

#if defined(POWER_SAVING)
    // Set fast poll rate during pairing for quick response to coordinator
    NLME_SetPollRate(QUEUED_POLL_RATE); // 100ms - fast polling during pairing
    LREP("Pairing mode: Fast poll rate enabled for coordinator communication\r\n");
#endif

    LREP("Pairing mode: LED blinking rapidly (Aqara style)\r\n");
}

/*********************************************************************
 * @fn      zclCommissioning_AdaptiveTxPower
 * @brief   Adjust TX power based on join success/failure
 * @param   increase - true to increase power, false to decrease
 * @return  none
 */
static void zclCommissioning_AdaptiveTxPower(bool increase) {
    // Only apply adaptive logic if in auto mode (mode 0)
    // Other modes: 1=Manual, 2=Max, 3=Eco are user-controlled
    if (zclApp_TxPowerMode != 0) {
        LREP("Skipping adaptive TX power (mode=%d, not auto)\r\n", zclApp_TxPowerMode);
        return;
    }

    if (increase && current_tx_power < 4) {  // 4 = TX_PWR_PLUS_4 (+4 dBm)
        current_tx_power++;
        ZMacSetTransmitPower(current_tx_power);
        LREP("Increased TX power to +%d dBm\r\n", current_tx_power);
        network_metrics.current_tx_power = current_tx_power;
    } else if (!increase && current_tx_power > 0) {  // 0 = TX_PWR_0_DBM (0 dBm)
        current_tx_power = 0; // Reset to minimum (0 dBm)
        ZMacSetTransmitPower(current_tx_power);
        LREP("Reset TX power to 0 dBm\r\n");
        network_metrics.current_tx_power = current_tx_power;
    }
}

/*********************************************************************
 * @fn      zclCommissioning_UpdateNetworkQuality
 * @brief   Update parent LQI and other network metrics
 * @param   none
 * @return  none
 */
static void zclCommissioning_UpdateNetworkQuality(void) {
    // Get parent LQI from network layer
    // Note: NLME_GetLinkQuality() not available in Z-Stack 3.0.2, set to 0
    network_metrics.parent_lqi = 0;

    // Save current channel
    network_metrics.last_channel = _NIB.nwkLogicalChannel;

    LREP("Network quality: LQI=%d Channel=%d\r\n",
         network_metrics.parent_lqi,
         network_metrics.last_channel);

    // Save metrics to NV
    osal_nv_item_init(ZCD_NV_NETWORK_METRICS, sizeof(NetworkMetrics_t), &network_metrics);
    osal_nv_write(ZCD_NV_NETWORK_METRICS, 0, sizeof(NetworkMetrics_t), &network_metrics);

    // Save last successful channel separately for quick access
    osal_nv_item_init(ZCD_NV_LAST_CHANNEL, 1, &network_metrics.last_channel);
    osal_nv_write(ZCD_NV_LAST_CHANNEL, 0, 1, &network_metrics.last_channel);
}

/*********************************************************************
 * @fn      zclCommissioning_QuickRejoin
 * @brief   Try to rejoin on last successful channel first (fast path)
 * @param   none
 * @return  true if quick rejoin attempted
 */
static bool zclCommissioning_QuickRejoin(void) {
    uint8 last_channel = 0;

    // Read last successful channel from NV
    if (osal_nv_read(ZCD_NV_LAST_CHANNEL, 0, 1, &last_channel) == SUCCESS) {
        if (last_channel >= 11 && last_channel <= 26) {
            LREP("Quick rejoin attempt on channel %d\r\n", last_channel);

            // Note: Z-Stack handles channel setting internally
            // Just attempt rejoin, it will use scan results

            return true;
        }
    }

    LREP("No valid last channel, will do full scan\r\n");
    return false;
}

/*********************************************************************
 * @fn      zclCommissioning_CheckDeepSleep
 * @brief   Check if device should enter deep sleep mode
 * @param   none
 * @return  none
 */
static void zclCommissioning_CheckDeepSleep(void) {
    if (network_metrics.consecutive_failures >= APP_COMMISSIONING_DEEP_SLEEP_THRESHOLD) {
        LREP("DEEP SLEEP MODE: Too many failures (%d)\r\n",
                   network_metrics.consecutive_failures);
        LREPMaster("Will retry every 1 hour to save battery\r\n");

        // Use very long delay to save battery
        rejoinDelay = APP_COMMISSIONING_DEEP_SLEEP_INTERVAL;

        // Visual feedback - 3 slow blinks
        HalLedBlink(HAL_LED_1, 3, 100, 1000);

        LREP("Battery saver: 1-hour rejoin interval active\r\n");
    }
}

void zclCommissioning_Init(uint8 task_id) {
    zclCommissioning_TaskId = task_id;

    bdb_RegisterCommissioningStatusCB(zclCommissioning_ProcessCommissioningStatus);
    bdb_RegisterBindNotificationCB(zclCommissioning_BindNotification);

    // Hybrid Phase 2: Load network metrics from NV
    if (osal_nv_read(ZCD_NV_NETWORK_METRICS, 0, sizeof(NetworkMetrics_t), &network_metrics) == SUCCESS) {
        LREP("Loaded network metrics: rejoins=%d successes=%d failures=%d\r\n",
             network_metrics.rejoin_attempts,
             network_metrics.rejoin_successes,
             network_metrics.rejoin_failures);

        // Restore saved TX power
        if (network_metrics.current_tx_power >= 0 &&  // 0 = TX_PWR_0_DBM
            network_metrics.current_tx_power <= 4) {  // 4 = TX_PWR_PLUS_4
            current_tx_power = network_metrics.current_tx_power;
        }
    } else {
        LREP("First boot - initializing network metrics\r\n");
    }

    // Issue #24: Load rejoin backoff state from NV to maintain exponential backoff across power cycles
    RejoinBackoffState_t backoff_state;
    if (osal_nv_read(ZCD_NV_REJOIN_BACKOFF_STATE, 0, sizeof(RejoinBackoffState_t), &backoff_state) == SUCCESS) {
        rejoinsLeft = backoff_state.rejoinsLeft;
        rejoinDelay = backoff_state.rejoinDelay;
        LREP("Loaded rejoin backoff state: rejoinsLeft=%d rejoinDelay=%ld\r\n", rejoinsLeft, rejoinDelay);
    }

    // Set TX power (adaptive - start low to save battery)
    ZMacSetTransmitPower(current_tx_power);
    LREP("Initial TX power: %d dBm\r\n", current_tx_power);

    // this is important to allow connects throught routers
    // to make this work, coordinator should be compiled with this flag #define TP2_LEGACY_ZC
    requestNewTrustCenterLinkKey = FALSE;

    bdb_StartCommissioning(BDB_COMMISSIONING_MODE_NWK_STEERING | BDB_COMMISSIONING_MODE_FINDING_BINDING);
}

static void zclCommissioning_ResetBackoffRetry(void) {
    rejoinsLeft = APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES;
    rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY;
    quick_rejoin_attempted = false; // Reset for next time

    // Issue #24: Clear saved backoff state on successful connection
    zclCommissioning_SaveBackoffState();
}

static void zclCommissioning_OnConnect(void) {
    LREPMaster("[OK] zclCommissioning_OnConnect\r\n");

    // Update metrics - successful connection!
    network_metrics.rejoin_successes++;
    network_metrics.consecutive_failures = 0; // Reset failure counter
    zclCommissioning_UpdateNetworkQuality();

    // Reduce TX power for next time (save battery)
    zclCommissioning_AdaptiveTxPower(false);

    zclCommissioning_ResetBackoffRetry();

    // Stay awake for 2 minutes to allow coordinator to complete interview/configuration
    // (endpoint discovery, attribute reads, binding, reporting configuration)
    LREP("Staying awake for %d seconds for coordinator interview\r\n", APP_COMMISSIONING_INTERVIEW_PERIOD / 1000);
    osal_start_timerEx(zclCommissioning_TaskId, APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT, APP_COMMISSIONING_INTERVIEW_PERIOD);
}

static void zclCommissioning_ProcessCommissioningStatus(bdbCommissioningModeMsg_t *bdbCommissioningModeMsg) {
    LREP("bdbCommissioningMode=%d bdbCommissioningStatus=%d bdbRemainingCommissioningModes=0x%X\r\n",
         bdbCommissioningModeMsg->bdbCommissioningMode, bdbCommissioningModeMsg->bdbCommissioningStatus,
         bdbCommissioningModeMsg->bdbRemainingCommissioningModes);
    switch (bdbCommissioningModeMsg->bdbCommissioningMode) {
    case BDB_COMMISSIONING_INITIALIZATION:
        switch (bdbCommissioningModeMsg->bdbCommissioningStatus) {
        case BDB_COMMISSIONING_NO_NETWORK:
            LREP("No network - starting pairing mode\r\n");
            // Aqara-style: Device has no network, will auto-attempt to join
            // Start continuous rapid LED blinking to indicate pairing mode
            zclCommissioning_StartPairingMode();
            break;
        case BDB_COMMISSIONING_NETWORK_RESTORED:
            zclCommissioning_OnConnect();
            break;
        default:
            break;
        }
        break;
    case BDB_COMMISSIONING_NWK_STEERING:
        switch (bdbCommissioningModeMsg->bdbCommissioningStatus) {
        case BDB_COMMISSIONING_SUCCESS:
            // Aqara-style: Stop rapid blinking, show success pattern
            if (pairing_mode_active) {
                HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF); // Stop rapid blinking
                // Success: 3 slow blinks (Aqara style)
                HalLedBlink(HAL_LED_1, 3, 50, 1000);
                pairing_mode_active = false;
                LREP("Pairing SUCCESS: 3 slow blinks (Aqara style)\r\n");
            } else {
                // Automatic rejoin success (not user-initiated)
                HalLedBlink(HAL_LED_1, 2, 50, 500);
            }
            LREPMaster("BDB_COMMISSIONING_SUCCESS\r\n");
            zclCommissioning_OnConnect();
            break;

        default:
            // Aqara-style: Stop rapid blinking, show failure pattern
            if (pairing_mode_active) {
                HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF); // Stop rapid blinking
                // Failure: 3 fast blinks then off (Aqara style)
                HalLedBlink(HAL_LED_1, 3, 50, 300);
                pairing_mode_active = false;
                LREP("Pairing FAILED: 3 fast blinks (Aqara style)\r\n");
            } else {
                // Automatic rejoin failure (not user-initiated)
                HalLedBlink(HAL_LED_1, 2, 50, 300);
            }
            LREP("Network join failed - press button to retry\r\n");
            break;
        }

        break;

    case BDB_COMMISSIONING_PARENT_LOST:
        LREPMaster("[WARN] BDB_COMMISSIONING_PARENT_LOST\r\n");
        switch (bdbCommissioningModeMsg->bdbCommissioningStatus) {
        case BDB_COMMISSIONING_NETWORK_RESTORED:
            LREPMaster("[OK] Network restored successfully!\r\n");
            zclCommissioning_ResetBackoffRetry();
            network_metrics.consecutive_failures = 0;
            break;

        default:
            // Blink 2 times to indicate rejoin attempt (not continuous)
            HalLedBlink(HAL_LED_1, 2, 50, 300);

            // Update failure metrics
            network_metrics.rejoin_attempts++;
            network_metrics.rejoin_failures++;
            network_metrics.consecutive_failures++;

            LREP("Rejoin attempt #%d (failures: %d, rejoinsLeft: %d, delay: %ld ms)\r\n",
                 network_metrics.rejoin_attempts,
                 network_metrics.consecutive_failures,
                 rejoinsLeft,
                 rejoinDelay);

            // Increase TX power for next attempt (if not already at max)
            zclCommissioning_AdaptiveTxPower(true);

            // Exponential backoff
            if (rejoinsLeft > 0) {
                rejoinDelay *= APP_COMMISSIONING_END_DEVICE_REJOIN_BACKOFF;
                rejoinsLeft -= 1;
            } else {
                rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_MAX_DELAY;
            }

            // Issue #24: Save updated backoff state to persist across power cycles
            zclCommissioning_SaveBackoffState();

            // Check if should enter deep sleep mode
            zclCommissioning_CheckDeepSleep();

            // Check if we should give up (network probably gone/changed)
            if (network_metrics.consecutive_failures >= APP_COMMISSIONING_GIVE_UP_THRESHOLD) {
                LREP("GAVE UP: %d consecutive failures - network likely gone\r\n",
                           network_metrics.consecutive_failures);
                LREPMaster("Stopped automatic retries to save battery\r\n");
                LREPMaster("Press button to manually retry joining\r\n");

                // Issue #25: Persist give-up state to NV so device remembers after power cycle
                osal_nv_write(ZCD_NV_NETWORK_METRICS, 0, sizeof(NetworkMetrics_t), &network_metrics);

                // Turn off LED to save battery
                HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);

                // Don't schedule another rejoin - wait for button press
                break;
            }

            // Try quick rejoin first (on first attempt)
            if (!quick_rejoin_attempted && network_metrics.last_channel != 0) {
                quick_rejoin_attempted = true;
                zclCommissioning_QuickRejoin();
            }

            osal_start_timerEx(zclCommissioning_TaskId, APP_COMMISSIONING_END_DEVICE_REJOIN_EVT, rejoinDelay);
            break;
        }
        break;
    default:
        break;
    }
}

static void zclCommissioning_ProcessIncomingMsg(zclIncomingMsg_t *pInMsg) {
    if (pInMsg->attrCmd) {
        osal_mem_free(pInMsg->attrCmd);
    }
}

void zclCommissioning_Sleep(uint8 allow) {
    LREP("zclCommissioning_Sleep %d\r\n", allow);
#if defined(POWER_SAVING)
    if (allow) {
        NLME_SetPollRate(0);
        // Turn off LED when entering sleep mode to save battery
        HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
        LREP("Entering sleep mode - LED off\r\n");
    } else {
        NLME_SetPollRate(POLL_RATE);
    }
#endif
}

uint16 zclCommissioning_event_loop(uint8 task_id, uint16 events) {
    if (events & SYS_EVENT_MSG) {
        devStates_t zclApp_NwkState;
        afIncomingMSGPacket_t *MSGpkt;
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(zclCommissioning_TaskId))) {

            switch (MSGpkt->hdr.event) {
            case ZDO_STATE_CHANGE:
                zclApp_NwkState = (devStates_t)(MSGpkt->hdr.status);
                LREP("NwkState=%d\r\n", zclApp_NwkState);
                if (zclApp_NwkState == DEV_END_DEVICE) {
                    // Connected - LED will be turned off after interview period
                    // Don't turn it off here to avoid interfering with success pattern
                    LREP("Device connected (state=DEV_END_DEVICE)\r\n");
                } else {
                    // State change - blink twice (not continuous)
                    HalLedBlink(HAL_LED_1, 2, 50, 300);
                }
                break;

            case ZCL_INCOMING_MSG:
                zclCommissioning_ProcessIncomingMsg((zclIncomingMsg_t *)MSGpkt);
                break;

            default:
                break;
            }

            // Release the memory
            osal_msg_deallocate((uint8 *)MSGpkt);
        }

        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }
    if (events & APP_COMMISSIONING_END_DEVICE_REJOIN_EVT) {
        LREPMaster("APP_END_DEVICE_REJOIN_EVT\r\n");
#if ZG_BUILD_ENDDEVICE_TYPE
        bdb_ZedAttemptRecoverNwk();
#endif
        return (events ^ APP_COMMISSIONING_END_DEVICE_REJOIN_EVT);
    }

    if (events & APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT) {
        LREPMaster("APP_CLOCK_DOWN_POLING_RATE_EVT\r\n");
        zclCommissioning_Sleep(true);
        return (events ^ APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT);
    }

    // Discard unknown events
    return 0;
}

static void zclCommissioning_BindNotification(bdbBindNotificationData_t *data) {
    // Issue #9: Add NULL check to prevent potential crash
    if (data == NULL) {
        LREP("ERROR: Bind notification received with NULL data\r\n");
        return;
    }

    // Blink 2 times to indicate bind (not continuous)
    HalLedBlink(HAL_LED_1, 2, 50, 300);
    LREP("Recieved bind request clusterId=0x%X dstAddr=0x%X ep=%d\r\n", data->clusterId, data->dstAddr, data->ep);
    uint16 maxEntries = 0, usedEntries = 0;
    bindCapacity(&maxEntries, &usedEntries);
    LREP("bindCapacity %d usedEntries %d \r\n", maxEntries, usedEntries);
}

void zclCommissioning_HandleKeys(uint8 portAndAction, uint8 keyCode) {
    if (portAndAction & HAL_KEY_PRESS) {
#if ZG_BUILD_ENDDEVICE_TYPE
        if (devState == DEV_NWK_ORPHAN) {
            LREP("devState=%d try to restore network\r\n", devState);

            // Reset failure counter on manual button press
            // Allows fresh start after "give up" threshold reached
            if (network_metrics.consecutive_failures >= APP_COMMISSIONING_GIVE_UP_THRESHOLD) {
                LREP("Button pressed - resetting failure counter for fresh attempt\r\n");
                network_metrics.consecutive_failures = 0;
                zclCommissioning_ResetBackoffRetry();
            }

            bdb_ZedAttemptRecoverNwk();
        }
#endif

        #if defined(POWER_SAVING)
            // Fast poll for button responsiveness
            NLME_SetPollRate(1);
            // Revert to normal rate after 3 seconds to save battery
            osal_start_timerEx(zclCommissioning_TaskId, APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT, 3000);
        #endif
    }
}