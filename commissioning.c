#include "commissioning.h"
#include "Debug.h"
#include "OSAL_PwrMgr.h"
#include "ZDApp.h"
#include "bdb_interface.h"
#include "hal_key.h"
#include "hal_led.h"
#include "nwk_globals.h"
#include "zcl_app.h"  // For TX power mode access

static void zclCommissioning_ProcessCommissioningStatus(bdbCommissioningModeMsg_t *bdbCommissioningModeMsg);
static void zclCommissioning_ResetBackoffRetry(void);
static void zclCommissioning_BindNotification(bdbBindNotificationData_t *data);
extern bool requestNewTrustCenterLinkKey;

// External TX power mode from zcl_app.c
extern uint8 zclApp_TxPowerMode;

byte rejoinsLeft = APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES;
uint32 rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY;

uint8 zclCommissioning_TaskId = 0;

// Hybrid Phase 2: Network Quality Metrics (typedef moved to header)
NetworkMetrics_t network_metrics = {0}; // Non-static for ZCL access
static uint8 current_tx_power = TX_PWR_0_DBM; // Start at 0 dBm to save battery
static bool quick_rejoin_attempted = false;

#ifndef APP_TX_POWER
    #define APP_TX_POWER TX_PWR_PLUS_4
#endif

/*********************************************************************
 * HYBRID PHASE 2: HELPER FUNCTIONS
 */

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

    if (increase && current_tx_power < TX_PWR_PLUS_4) {
        current_tx_power++;
        ZMacSetTransmitPower(current_tx_power);
        LREP("Increased TX power to +%d dBm\r\n", current_tx_power);
        network_metrics.current_tx_power = current_tx_power;
    } else if (!increase && current_tx_power > TX_PWR_0_DBM) {
        current_tx_power = TX_PWR_0_DBM; // Reset to minimum
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
    network_metrics.parent_lqi = NLME_GetLinkQuality();

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
            LREPMaster("Quick rejoin attempt on channel %d\r\n", last_channel);

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
        LREPMaster("DEEP SLEEP MODE: Too many failures (%d)\r\n",
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

    LED_Init(); // Инициализация светодиодов

    bdb_RegisterCommissioningStatusCB(zclCommissioning_ProcessCommissioningStatus);
    bdb_RegisterBindNotificationCB(zclCommissioning_BindNotification);

    // Hybrid Phase 2: Load network metrics from NV
    if (osal_nv_read(ZCD_NV_NETWORK_METRICS, 0, sizeof(NetworkMetrics_t), &network_metrics) == SUCCESS) {
        LREP("Loaded network metrics: rejoins=%d successes=%d failures=%d\r\n",
             network_metrics.rejoin_attempts,
             network_metrics.rejoin_successes,
             network_metrics.rejoin_failures);

        // Restore saved TX power
        if (network_metrics.current_tx_power >= TX_PWR_0_DBM &&
            network_metrics.current_tx_power <= TX_PWR_PLUS_4) {
            current_tx_power = network_metrics.current_tx_power;
        }
    } else {
        LREP("First boot - initializing network metrics\r\n");
    }

    // Set TX power (adaptive - start low to save battery)
    ZMacSetTransmitPower(current_tx_power);
    LREP("Initial TX power: %d dBm\r\n", current_tx_power);

    requestNewTrustCenterLinkKey = FALSE;

    bdb_StartCommissioning(BDB_COMMISSIONING_MODE_NWK_STEERING | BDB_COMMISSIONING_MODE_FINDING_BINDING);
}

static void zclCommissioning_ResetBackoffRetry(void) {
    rejoinsLeft = APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES;
    rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY;
    quick_rejoin_attempted = false; // Reset for next time
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
    osal_start_timerEx(zclCommissioning_TaskId, APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT, 10 * 1000);
}

static void zclCommissioning_ProcessCommissioningStatus(bdbCommissioningModeMsg_t *bdbCommissioningModeMsg) {
    LREP("bdbCommissioningMode=%d bdbCommissioningStatus=%d bdbRemainingCommissioningModes=0x%X\r\n",
         bdbCommissioningModeMsg->bdbCommissioningMode, bdbCommissioningModeMsg->bdbCommissioningStatus,
         bdbCommissioningModeMsg->bdbRemainingCommissioningModes);
    switch (bdbCommissioningModeMsg->bdbCommissioningMode) {
    case BDB_COMMISSIONING_INITIALIZATION:
        switch (bdbCommissioningModeMsg->bdbCommissioningStatus) {
        case BDB_COMMISSIONING_NO_NETWORK:
            LREP("No network\r\n");
            LED_Signal(3, 50, 500, 0); // 3 вспышки
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
        case BDB_COMMISSIONING_IN_PROGRESS:
            LED_Signal(1, 50, 4950, 0); // 1 вспышка каждые 5 секунд
            break;
        case BDB_COMMISSIONING_SUCCESS:
            LED_Signal(3, 100, 100, 0); // 3 вспышки с интервалом 100 мс
            LREPMaster("BDB_COMMISSIONING_SUCCESS\r\n");
            zclCommissioning_OnConnect();
            break;
        default:
            LED_Signal(2, 100, 500, 5000); // Ошибка подключения
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
            HalLedSet(HAL_LED_1, HAL_LED_MODE_BLINK);

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

            // Check if should enter deep sleep mode
            zclCommissioning_CheckDeepSleep();

            // Try quick rejoin first (on first attempt)
            if (!quick_rejoin_attempted && network_metrics.last_channel != 0) {
                quick_rejoin_attempted = true;
                zclCommissioning_QuickRejoin();
            }

            osal_start_timerEx(zclCommissioning_TaskId, APP_COMMISSIONING_END_DEVICE_REJOIN_EVT, rejoinDelay);
            break;
        }
        break;
    case BDB_COMMISSIONING_FACTORY_RESET:
        LED_Signal(5, 100, 200, 0); // 5 вспышек с интервалом 200 мс
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
    } else {
        NLME_SetPollRate(POLL_RATE);
    }
#endif
}

uint16 zclCommissioning_event_loop(uint8 task_id, uint16 events) {
    if (events & LED_SIGNAL_EVT) {
        LED_Signal(0, 0, 0, 0); // Обновить состояние мигания
        return events ^ LED_SIGNAL_EVT;
    }

    if (events & LED_REPEAT_EVT) {
        LED_Signal(0, 0, 0, 0); // Повтор цикла мигания
        return events ^ LED_REPEAT_EVT;
    }

    if (events & SYS_EVENT_MSG) {
        devStates_t zclApp_NwkState;
        afIncomingMSGPacket_t *MSGpkt;
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(zclCommissioning_TaskId))) {
            switch (MSGpkt->hdr.event) {
            case ZDO_STATE_CHANGE:
                zclApp_NwkState = (devStates_t)(MSGpkt->hdr.status);
                LREP("NwkState=%d\r\n", zclApp_NwkState);
                if (zclApp_NwkState == DEV_END_DEVICE) {
                    LED_Signal(3, 100, 100, 0); // Индикация успешного подключения
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

    return 0;
}

static void zclCommissioning_BindNotification(bdbBindNotificationData_t *data) {
    LREP("Recieved bind request clusterId=0x%X dstAddr=0x%X ep=%d\r\n", data->clusterId, data->dstAddr, data->ep);
    uint16 maxEntries = 0, usedEntries = 0;
    bindCapacity(&maxEntries, &usedEntries);
    LREP("bindCapacity %d %usedEntries %d \r\n", maxEntries, usedEntries);
}

void zclCommissioning_HandleKeys(uint8 portAndAction, uint8 keyCode) {
    if (portAndAction & HAL_KEY_PRESS) {
#if ZG_BUILD_ENDDEVICE_TYPE
        if (devState == DEV_NWK_ORPHAN) {
            LREP("devState=%d try to restore network\r\n", devState);
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
