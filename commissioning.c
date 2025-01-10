#include "commissioning.h"
#include "Debug.h"
#include "OSAL_PwrMgr.h"
#include "ZDApp.h"
#include "bdb_interface.h"
#include "hal_key.h"
#include "hal_led.h"
#include "led_control.h"

static void zclCommissioning_ProcessCommissioningStatus(bdbCommissioningModeMsg_t *bdbCommissioningModeMsg);
static void zclCommissioning_ResetBackoffRetry(void);
static void zclCommissioning_BindNotification(bdbBindNotificationData_t *data);
extern bool requestNewTrustCenterLinkKey;

byte rejoinsLeft = APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES;
uint32 rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY;

uint8 zclCommissioning_TaskId = 0;

#ifndef APP_TX_POWER
    #define APP_TX_POWER TX_PWR_PLUS_4
#endif

void zclCommissioning_Init(uint8 task_id) {
    zclCommissioning_TaskId = task_id;

    LED_Init(); // Инициализация светодиодов

    bdb_RegisterCommissioningStatusCB(zclCommissioning_ProcessCommissioningStatus);
    bdb_RegisterBindNotificationCB(zclCommissioning_BindNotification);

    ZMacSetTransmitPower(APP_TX_POWER);

    requestNewTrustCenterLinkKey = FALSE;
    bdb_StartCommissioning(BDB_COMMISSIONING_MODE_NWK_STEERING | BDB_COMMISSIONING_MODE_FINDING_BINDING);
}

static void zclCommissioning_ResetBackoffRetry(void) {
    rejoinsLeft = APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES;
    rejoinDelay = APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY;
}

static void zclCommissioning_OnConnect(void) {
    LREPMaster("zclCommissioning_OnConnect \r\n");
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
        LREPMaster("BDB_COMMISSIONING_PARENT_LOST\r\n");
        LED_Signal(1, 50, 9950, 0); // 1 вспышка каждые 10 секунд
        switch (bdbCommissioningModeMsg->bdbCommissioningStatus) {
        case BDB_COMMISSIONING_NETWORK_RESTORED:
            zclCommissioning_ResetBackoffRetry();
            break;
        default:
            rejoinDelay *= APP_COMMISSIONING_END_DEVICE_REJOIN_BACKOFF;
            rejoinsLeft -= 1;
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
