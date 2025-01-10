// ds18b20.c
#include "ds18b20.h"
#include "OnBoard.h"

#define DS18B20_SKIP_ROM 0xCC
#define DS18B20_CONVERT_T 0x44
#define DS18B20_READ_SCRATCHPAD 0xBE
#define DS18B20_WRITE_SCRATCHPAD 0x4E

// Device resolution
#define DS18B20_TEMP_9_BIT 0x1F  // 9 bit
#define DS18B20_TEMP_10_BIT 0x3F // 10 bit
#define DS18B20_TEMP_11_BIT 0x5F // 11 bit
#define DS18B20_TEMP_12_BIT 0x7F // 12 bit

#ifndef DS18B20_RESOLUTION
#define DS18B20_RESOLUTION DS18B20_TEMP_10_BIT
#endif

#ifndef DS18B20_RETRY_COUNT
#define DS18B20_RETRY_COUNT 10
#endif

#define MAX_CONVERSION_TIME (750 * 1.2) // ms 750ms + some overhead

#define DS18B20_RETRY_DELAY ((uint16) (MAX_CONVERSION_TIME / DS18B20_RETRY_COUNT))

static void _delay_us(uint16);
static void _delay_ms(uint16);
static void ds18b20_send(uint8);
static uint8 ds18b20_read(void);
static void ds18b20_send_byte(uint8);
static uint8 ds18b20_read_byte(void);
static uint8 ds18b20_Reset(void);
static void ds18b20_setResolution(uint8 resolution);
static int16 ds18b20_convertTemperature(uint8 temp1, uint8 temp2, uint8 resolution);

bool ds18b20_isPresent(void)
{
    return ds18b20_Reset() == 0; // Check for sensor presence
}

static void _delay_us(uint16 microSecs)
{
    while (microSecs--)
    {
        halMcuWaitUs(1); // Use a more accurate built-in delay
    }
}

static void _delay_ms(uint16 milliSecs)
{
    while (milliSecs--) {
        _delay_us(1000);
    }
}

// Sends one bit to bus
static void ds18b20_send(uint8 bit) {
    TSENS_SBIT = 1;
    TSENS_DIR |= TSENS_BV; // output
    TSENS_SBIT = 0;
    if (bit != 0)
        _delay_us(8);
    else
        _delay_us(80);
    TSENS_SBIT = 1;
    if (bit != 0)
        _delay_us(80);
    else
        _delay_us(2);
}

// Reads one bit from bus
static uint8 ds18b20_read(void) {
    TSENS_SBIT = 1;
    TSENS_DIR |= TSENS_BV; // output
    TSENS_SBIT = 0;
    _delay_us(2);
    TSENS_DIR &= ~TSENS_BV; // input
    _delay_us(5);
    uint8 i = TSENS_SBIT;
    _delay_us(60);
    return i;
}

// Sends one byte to bus
static void ds18b20_send_byte(uint8 data) {
    for (uint8 i = 0; i < 8; i++) {
        ds18b20_send(data & (1 << i));
    }
}

// Reads one byte from bus
static uint8 ds18b20_read_byte(void) {
    uint8 data = 0;
    for (uint8 i = 0; i < 8; i++) {
        if (ds18b20_read()) {
            data |= (1 << i);
        }
    }
    return data;
}

// Sends reset pulse
static uint8 ds18b20_Reset(void) {
    TSENS_DIR |= TSENS_BV; // output
    TSENS_SBIT = 0;
    _delay_us(500);
    TSENS_DIR &= ~TSENS_BV; // input
    _delay_us(70);
    uint8 i = TSENS_SBIT;
    _delay_us(200);
    return i;
}

static void ds18b20_setResolution(uint8 resolution) {
    ds18b20_Reset();
    ds18b20_send_byte(DS18B20_SKIP_ROM);
    ds18b20_send_byte(DS18B20_WRITE_SCRATCHPAD);
    ds18b20_send_byte(0);  // Low alarm
    ds18b20_send_byte(100); // High alarm
    ds18b20_send_byte(resolution);
}

static int16 ds18b20_convertTemperature(uint8 temp1, uint8 temp2, uint8 resolution) {
    int16 temperature = 0;
    uint8 ignoreMask = 0;
    switch (resolution) {
    case DS18B20_TEMP_9_BIT:
        ignoreMask = (1 << 0) | (1 << 1) | (1 << 2);
        break;
    case DS18B20_TEMP_10_BIT:
        ignoreMask = (1 << 0) | (1 << 1);
        break;
    case DS18B20_TEMP_11_BIT:
        ignoreMask = (1 << 0);
        break;
    case DS18B20_TEMP_12_BIT:
        ignoreMask = 0;
        break;
    default:
        break;
    }
    temperature = (uint16)temp1 | ((uint16)(ignoreMask ? temp2 & ignoreMask : temp2) << 8);
    if (temp2 & (1 << 3)) { // Negative temperature
        temperature = (temperature / 16) - 128;
    } else { // Positive temperature
        temperature = temperature / 16;
    }
    return temperature * 100;
}

int16 readTemperature(void) {
    uint8 temp1, temp2, retry_count = DS18B20_RETRY_COUNT;

    if (!ds18b20_isPresent()) {
        return -1; // Sensor not found
    }

    ds18b20_setResolution(DS18B20_RESOLUTION);
    ds18b20_Reset();
    ds18b20_send_byte(DS18B20_SKIP_ROM);
    ds18b20_send_byte(DS18B20_CONVERT_T);

    while (retry_count--) {
        _delay_ms(DS18B20_RETRY_DELAY);
        ds18b20_Reset();
        ds18b20_send_byte(DS18B20_SKIP_ROM);
        ds18b20_send_byte(DS18B20_READ_SCRATCHPAD);

        temp1 = ds18b20_read_byte();
        temp2 = ds18b20_read_byte();

        if (temp1 == 0xFF && temp2 == 0xFF) {
            return -2; // No data
        }
        if (temp1 == 0x50 && temp2 == 0x05) {
            continue; // Not ready
        }
        return ds18b20_convertTemperature(temp1, temp2, DS18B20_RESOLUTION);
    }

    return -3; // Timeout
}
