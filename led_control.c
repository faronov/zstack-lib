// led_control.c
#include "led_control.h"
#include "hal_board_cfg.h"
#include "OSAL.h"

#define LED_SIGNAL_EVT 0x0001
#define LED_REPEAT_EVT 0x0002

void LED_Init(void)
{
    LED1_DDR |= LED1_BV; // Установить пин LED1 как выход
    LED_Off();           // Убедиться, что светодиод выключен
}

void LED_On(void)
{
    if (LED1_POLARITY == ACTIVE_HIGH)
    {
        LED1_SBIT = 1; // Включить светодиод
    }
    else
    {
        LED1_SBIT = 0; // Включить светодиод
    }
}

void LED_Off(void)
{
    if (LED1_POLARITY == ACTIVE_HIGH)
    {
        LED1_SBIT = 0; // Выключить светодиод
    }
    else
    {
        LED1_SBIT = 1; // Выключить светодиод
    }
}

void LED_Signal(uint8_t times, uint16_t onDuration, uint16_t offDuration, uint8_t repeatInterval)
{
    static uint8_t blinkCount = 0;

    if (blinkCount < times * 2)
    {
        if (blinkCount % 2 == 0)
        {
            LED_On(); // Включить светодиод
            osal_start_timerEx(zclApp_TaskID, LED_SIGNAL_EVT, onDuration);
        }
        else
        {
            LED_Off(); // Выключить светодиод
            osal_start_timerEx(zclApp_TaskID, LED_SIGNAL_EVT, offDuration);
        }
        blinkCount++;
    }
    else
    {
        blinkCount = 0;
        LED_Off(); // Убедиться, что светодиод выключен
        if (repeatInterval > 0)
        {
            osal_start_timerEx(zclApp_TaskID, LED_REPEAT_EVT, repeatInterval);
        }
    }
}
