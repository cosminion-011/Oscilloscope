# ESP32-powered 33V Oscilloscope
Code and instructions to build an oscilloscope to measure 33V max.

# Hardware Components
- ESP32 DevKit V1;
- 2.8" QVGA TFT LCD with an ILI9341 display;
- x10 100kΩ resistors and x1 10kΩ;
- 3 buttons;
- x2 9x15cm perfoboard;
- Copper wire.

# Wiring
1. Connect the LCD screen and buttons to the ESP32 according to the code's given pins;
2. You will need 2 probe tips to measure the voltage: connect one directly to GND:
3. Use the voltage divider (with the 100k resistors): probe tip -> x9 100k resistor in series -> (wire going to GPIO in between) -> 100k resistor -> GND.
