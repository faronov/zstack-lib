#ifndef commissioning_h
#define commissioning_h

#define APP_COMMISSIONING_CLOCK_DOWN_POLING_RATE_EVT  0x0001
#define APP_COMMISSIONING_END_DEVICE_REJOIN_EVT       0x0002

// Enhanced rejoin strategy (Hybrid Phase 2)
#define APP_COMMISSIONING_END_DEVICE_REJOIN_MAX_DELAY ((uint32)900000) // 15 minutes (reduced from 30 for battery)
#define APP_COMMISSIONING_END_DEVICE_REJOIN_START_DELAY 10 * 1000 // 10 seconds
#define APP_COMMISSIONING_END_DEVICE_REJOIN_BACKOFF ((float) 1.5) // More aggressive (was 1.2)
#define APP_COMMISSIONING_END_DEVICE_REJOIN_TRIES 30 // Increased from 20

// Deep sleep mode after many failures
#define APP_COMMISSIONING_DEEP_SLEEP_THRESHOLD 50 // After 50 consecutive failures
#define APP_COMMISSIONING_DEEP_SLEEP_INTERVAL ((uint32)3600000) // 1 hour between retries

// Give up threshold - assume network is gone (coordinator reset, etc.)
#define APP_COMMISSIONING_GIVE_UP_THRESHOLD 150 // After 150 consecutive failures (~6 days at 1hr intervals)
// After this, device stops retrying and waits for button press

// NV storage IDs for network metrics
#define ZCD_NV_NETWORK_METRICS 0x0403
#define ZCD_NV_LAST_CHANNEL 0x0404

// Note: TX_PWR_0_DBM through TX_PWR_PLUS_4 are defined in Z-Stack's ZMAC.h

// Hybrid Phase 2: Network Quality Metrics structure
typedef struct {
    uint8 parent_lqi;            // Link Quality Indicator (0-255)
    uint16 rejoin_attempts;      // Total rejoin attempts
    uint16 rejoin_successes;     // Successful rejoins
    uint16 rejoin_failures;      // Failed rejoins
    uint32 last_rejoin_time_ms;  // Time for last rejoin (ms)
    uint8 last_channel;          // Last successful channel (11-26)
    uint8 current_tx_power;      // Current TX power (0-4 for 0dBm to +4dBm)
    uint16 consecutive_failures; // Consecutive rejoin failures
} NetworkMetrics_t;

// Global network metrics (accessible for ZCL reporting)
extern NetworkMetrics_t network_metrics;

extern void zclCommissioning_Init(uint8 task_id);
extern uint16 zclCommissioning_event_loop(uint8 task_id, uint16 events);
extern void zclCommissioning_Sleep( uint8 allow );
extern void zclCommissioning_HandleKeys(uint8 portAndAction, uint8 keyCode);

#endif