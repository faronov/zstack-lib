# Collection of reusable components for Z-Stack 3.0.2

Shared library of hardware drivers and utilities for Z-Stack 3.0.2 projects.

## Modules

### Hardware Drivers
- **battery** - Battery voltage monitoring (VDD ADC reading)
- **ds18b20** - Dallas DS18B20 temperature sensor (1-Wire)
- **mhz19** - MH-Z19 CO2 sensor (UART)
- **senseair** - SenseAir CO2 sensor (UART)
- **hal_i2c** - I2C communication (software bitbang)

### System Components
- **commissioning** - Zigbee network join/rejoin with adaptive TX power
- **factory_reset** - Factory reset via button hold or boot counter
- **led_breathing** - LED effects for pairing mode
- **hal_key** - Button/key handling
- **tl_resetter** - Tuya/Livolo device reset logic

### Utilities
- **utils** - GPIO macros, ADC sampling, value mapping
- **Debug** - Debug logging macros (LREP, LREPMaster)

## How to compile
Follow this article https://zigdevwiki.github.io/Begin/IAR_install/

## Integration
Add files to your IAR project and include relevant headers in your application.
