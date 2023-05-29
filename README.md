## Hardwares
- ESP32 board
- CDS sensor
- Step motor (or Servo motor, with code modification)
  - 28BYJ-48 with ULN2003 shield
- 6V battery
- LEDs (3 red, 3 blue, 1 green or yellow)
- Resistors (2 220Ω, 1 1kΩ, 1 2kΩ)
- 2 Push buttons
- 16x2 LCD
  - LCM1602 IIC shield
- Piezo buzzer

## Getting Started
- Assemble circuit according to the attached diagram
- Install arduino dependencies
  - [LiquidCrystal_I2C](https://github.com/johnrickman/LiquidCrystal_I2C) by Frank de Brabander
  - [AsyncTCP](https://github.com/dvarrel/AsyncTCP) by dvarrel
  - [ESPAsyncWebServer](https://github.com/dvarrel/ESPAsyncWebSrv) by dvarrel
- Modify Wi-Fi credentials (FIXME comment in code)
- Compile and upload main_code.ino to ESP32 board
- (Optional) Open IP address printed in serial monitor from browser

## Demo
- https://youtu.be/Ze-bofUUB5A