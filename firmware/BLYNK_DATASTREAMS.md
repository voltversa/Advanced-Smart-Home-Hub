# Home HUB Blynk datastreams

Create every datastream below in **Developer Zone -> Templates -> Home HUB ->
Datastreams**. The batch update is rejected if the firmware requests a pin that
does not exist in this template.

| Pin | Name | Type | Units | Suggested range |
|---|---|---|---|---|
| V0 | Flame detected | Integer | Boolean | 0 to 1 |
| V1 | Flame ADC | Integer | ADC counts | 0 to 4095 |
| V2 | Flame voltage | Double | V | 0 to 3.3 |
| V3 | CO2 | Integer | ppm | 0 to 5000 |
| V4 | Room temperature (compensated SCD41) | Double | °C | -40 to 70 |
| V5 | Room humidity (compensated SCD41) | Double | % | 0 to 100 |
| V6 | BME688 PCB temperature diagnostic | Double | °C | -40 to 85 |
| V7 | BME688 PCB humidity diagnostic | Double | % | 0 to 100 |
| V8 | BME688 pressure | Double | hPa | 300 to 1100 |
| V9 | BME688 gas resistance | Double | kΩ | 0 to 10000 |
| V10 | Gas baseline ratio | Integer | % | 0 to 200 |
| V11 | Gas alarm | Integer | Boolean | 0 to 1 |
| V12 | Outside temperature | Integer | °C | -50 to 60 |
| V13 | Outside feels-like | Integer | °C | -50 to 60 |
| V14 | Outside daily high | Integer | °C | -50 to 60 |
| V15 | Outside daily low | Integer | °C | -50 to 60 |
| V16 | Weather theme | Integer | Index | 0 to 15 |

The pin numbers are centralized in `Core/Inc/app_config.h`. If you change a
pin in Blynk, change its matching `VPIN_...` definition there as well.
