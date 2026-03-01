#ifndef ds18b20_h
#define ds18b20_h

int16 readTemperature(void);
uint8 ds18b20_Reset(void);
void ds18b20_setResolution(uint8 resolution);

#endif
