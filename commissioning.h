#ifndef commissioning_h
#define commissioning_h

#define APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT  0x0001
#define APP_COMMISSIONING_END_DEVICE_REJOIN_EVT       0x0002
#define APP_COMMISSIONING_PAIRING_TIMEOUT_EVT         0x0004
#define APP_COMMISSIONING_POLL_NORMAL_EVT             0x0008
#define APP_COMMISSIONING_JOIN_FLASH_EVT              0x0010  // 3-flash join success pattern

// Enhanced rejoin strategy (Hybrid Phase 2)
#define APP_COMMISSIONING_END_DEVICE_REJOIN_MAX_DELAY ((uint32)900000) // 15 minutes (reduced from 30 for battery)
#define APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY 10 * 1000 // 10 seconds
#define APP_COMMISSIONING_END_DEVICE_REJOIN_BACKOFF ((float) 1.5) // More aggressive (was 1.2)
#define APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES 30 // Increased from 20

// Interview/configuration period after successful join
// Phase 1: QUEUED_POLL_RATE (100ms) for 60s — covers Z2M/ZHA interview
// Phase 2: POLL_RATE (60s) — normal operation
#define APP_COMMISSIONING_INTERVIEW_PERIOD ((uint32)60000) // 60 seconds for coordinator interview
#define APP_COMMISSIONING_PAIRING_TIMEOUT ((uint32)300000)  // 5 minutes max fast-blink pairing window

// Deep sleep mode after many failures
#define APP_COMMISSIONING_DEEP_SLEEP_THRESHOLD 50 // After 50 consecutive failures
#define APP_COMMISSIONING_DEEP_SLEEP_INTERVAL ((uint32)3600000) // 1 hour between retries

// Give up threshold - assume network is gone (coordinator reset, etc.)
#define APP_COMMISSIONING_GIVE_UP_THRESHOLD 150 // After 150 consecutive failures (~6 days at 1hr intervals)
// After this, device stops retrying and waits for button press

// NV storage IDs for network metrics
#define ZCD_NV_NETWORK_METRICS 0x0403
#define ZCD_NV_LAST_CHANNEL 0x0404
#define ZCD_NV_REJOIN_BACKOFF_STATE 0x0407
#define ZCD_NV_FW_VERSION_STAMP 0x0408  // Detect firmware update → auto-clear stale NV
#define FW_VERSION_STAMP_LEN 17         // ZCL string: 1 length byte + 16 chars "DD/MM/YYYY HH:MM"

// Note: TX_PWR_0_DBM through TX_PWR_PLUS_4 are defined in Z-Stack's ZMAC.h

// Hybrid Phase 2: Network Quality Metrics structure
typedef struct {
    uint8 parent_lqi;            // Link Quality Indicator (0-255)
    uint16 rejoin_attempts;      // Total rejoin attempts
    uint16 rejoin_successes;     // Successful rejoins
    uint16 rejoin_failures;      // Failed rejoins
    uint32 last_rejoin_time_ms;  // Time for last rejoin (ms)
    uint8 last_channel;          // Last successful channel (11-26)
    int8 current_tx_power;       // Current TX power in dBm (signed for PA devices)
    uint16 consecutive_failures; // Consecutive rejoin failures
} NetworkMetrics_t;

// Global network metrics (accessible for ZCL reporting)
extern NetworkMetrics_t network_metrics;

// Interview state flag — set during post-join fast-poll window.
// Used by app layer to avoid interfering with interview fast-poll.
extern bool zclCommissioning_interviewActive;

extern void zclCommissioning_Init(uint8 task_id);
extern uint16 zclCommissioning_event_loop(uint8 task_id, uint16 events);
extern void zclCommissioning_Sleep( uint8 allow );
extern void zclCommissioning_HandleKeys(uint8 portAndAction, uint8 keyCode);
extern void zclCommissioning_StartPairingMode(void); // Aqara-style pairing LED
extern void zclCommissioning_ResetState(void);       // Clear backoff/metrics for fresh join

#endif
