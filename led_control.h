// led_control.h
#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <stdint.h>

// Инициализация светодиода
void LED_Init(void);

// Управление светодиодом
void LED_On(void);
void LED_Off(void);

// Сигнализация светодиодом
void LED_Signal(uint8_t times, uint16_t onDuration, uint16_t offDuration, uint8_t repeatInterval);

#endif // LED_CONTROL_H
