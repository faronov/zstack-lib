// ds18b20.h
#ifndef DS18B20_H
#define DS18B20_H

#include <stdint.h>
#include <stdbool.h>

int16 readTemperature(void);
bool ds18b20_isPresent(void);

#endif // DS18B20_H
