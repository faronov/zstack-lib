#include "hal_i2c.h"
#include "Debug.h"
#include "ioCC2530.h"
#include "zcomdef.h"
#include "utils.h"

#define STATIC static

#if !defined HAL_I2C_RETRY_CNT
#define HAL_I2C_RETRY_CNT 3
#endif

// Настройки портов и пинов I2C
#ifndef OCM_CLK_PORT
#define OCM_CLK_PORT 0
#endif

#ifndef OCM_DATA_PORT
#define OCM_DATA_PORT 0
#endif

#ifndef OCM_CLK_PIN
#define OCM_CLK_PIN 5
#endif

#ifndef OCM_DATA_PIN
#define OCM_DATA_PIN 6
#endif

#define OCM_SCL BNAME(OCM_CLK_PORT, OCM_CLK_PIN)
#define OCM_SDA BNAME(OCM_DATA_PORT, OCM_DATA_PIN)

#define OCM_DATA_HIGH() { IO_DIR_PORT_PIN(OCM_DATA_PORT, OCM_DATA_PIN, IO_IN); }
#define OCM_DATA_LOW()  { IO_DIR_PORT_PIN(OCM_DATA_PORT, OCM_DATA_PIN, IO_OUT); OCM_SDA = 0; }

static uint8 s_xmemIsInit = 0;

// Перевод пинов в режим Hi-Z для отключения I2C
void HalI2CShutdown(void) {
    IO_DIR_PORT_PIN(OCM_DATA_PORT, OCM_DATA_PIN, IO_IN); // SDA -> Input
    IO_DIR_PORT_PIN(OCM_CLK_PORT, OCM_CLK_PIN, IO_IN);   // SCL -> Input
    LREP("I2C pins set to Hi-Z for low power consumption\r\n");
}

// Инициализация I2C
void HalI2CInit(void) {
    if (!s_xmemIsInit) {
        s_xmemIsInit = 1;

        // Перевод пинов в Hi-Z до начала работы
        HalI2CShutdown();
        LREP("I2C Initialized\r\n");
    }
}

// Функция отправки данных
int8 HalI2CSend(uint8 address, uint8 *buf, uint16 len) {
    hali2cSendDeviceAddress(address);
    hali2cSend(buf, len, NOSEND_START, SEND_STOP);
    return 0;
}

// Функция получения данных
int8 HalI2CReceive(uint8 address, uint8 *buf, uint16 len) {
    hali2cReceive(address, buf, len);
    return 0;
}

// Вспомогательные функции для управления I2C
static void hali2cSend(uint8 *buffer, uint16 len, uint8 sendStart, uint8 sendStop);
static void hali2cReceive(uint8 address, uint8 *buffer, uint16 len);
static void hali2cSendDeviceAddress(uint8 address);
static void hali2cStart(void);
static void hali2cStop(void);
static void hali2cWrite(bool dBit);
static bool hali2cRead(void);
static uint8 hali2cReceiveByte(void);
static void hali2cClock(bool dir);
static void hali2cWait(uint8 count);

// Функция передачи данных
static void hali2cSend(uint8 *buffer, uint16 len, uint8 sendStart, uint8 sendStop) {
    if (sendStart) hali2cStart();
    for (uint16 i = 0; i < len; i++) {
        hali2cWrite(buffer[i]);
    }
    if (sendStop) hali2cStop();
}

// Функция получения данных
static void hali2cReceive(uint8 address, uint8 *buffer, uint16 len) {
    hali2cStart();
    hali2cSendDeviceAddress(address | 0x01);
    for (uint16 i = 0; i < len; i++) {
        buffer[i] = hali2cReceiveByte();
    }
    hali2cStop();
}

// Установка адреса устройства
static void hali2cSendDeviceAddress(uint8 address) {
    hali2cStart();
    hali2cWrite(address & 0xFE);
}

// I2C Start Condition
static void hali2cStart(void) {
    OCM_DATA_HIGH();
    hali2cWait(10);
    OCM_SCL = 1;
    hali2cWait(10);
    OCM_DATA_LOW();
    hali2cWait(10);
    OCM_SCL = 0;
}

// I2C Stop Condition
static void hali2cStop(void) {
    OCM_DATA_LOW();
    hali2cWait(10);
    OCM_SCL = 1;
    hali2cWait(10);
    OCM_DATA_HIGH();
    hali2cWait(10);
}

// Запись бита в I2C
static void hali2cWrite(bool dBit) {
    if (dBit) {
        OCM_DATA_HIGH();
    } else {
        OCM_DATA_LOW();
    }
    hali2cClock(0);
}

// Чтение бита с I2C
static bool hali2cRead(void) {
    OCM_DATA_HIGH();
    hali2cClock(1);
    return OCM_SDA;
}

// Получение байта данных
static uint8 hali2cReceiveByte(void) {
    uint8 data = 0;
    for (int8 i = 7; i >= 0; i--) {
        if (hali2cRead()) {
            data |= (1 << i);
        }
    }
    return data;
}

// Тактовый сигнал I2C
static void hali2cClock(bool dir) {
    if (dir) {
        OCM_SCL = 1;
    }
    hali2cWait(5);
    OCM_SCL = 0;
}

// Задержка
static void hali2cWait(uint8 count) {
    while (count--) {
        asm("NOP");
    }
}
