# Smart Home Hub — Weather GUI, QSPI Assets and SDRAM Frames

> **Historical reference:** this document describes the 14 August 2026
> IP-geolocation/Open-Meteo revision from which the GUI assets were recovered.
> The final 16 August `Core/Src/main.c` in this repository instead fixes the
> displayed location to Brussels and uses `wttr.in`. Use this guide for the
> QSPI asset map, installer workflow, artwork, and generation tools; use the
> root README and current source for final application behaviour.

This is the recovered weather/asset application package. It preserves the
working SCD41, BME688, flame detector, buzzer, ESP-01, Blynk and double-buffered
LTDC code, then adds:

- approximate IP-based city/coordinates and timezone;
- local time and date;
- outside temperature, feels-like, daily high/low and condition;
- eight full-screen weather/time backgrounds (day, dawn, sunset, night,
  cloudy, rain, storm and snow);
- a card-free 480x272 GUI in the style requested;
- calmer, persistent air-quality alarm thresholds;
- outside readings on Blynk V12 through V16.

No weather API key or ESP SSL support is needed. Location is requested from
the free HTTP `ip-api.com` JSON endpoint and weather from a compact one-day
Open-Meteo forecast containing only the fields used by the dashboard.

References:

- <https://ip-api.com/docs/api:json>
- <https://open-meteo.com/en/docs>

IP geolocation is approximate. A VPN, mobile network or ISP exit point can
produce a nearby city instead of the exact building. See “fixed location”
below if exact coordinates are required.

## What each memory does

| Memory | Address | Job |
|---|---:|---|
| STM32 internal Flash | `0x08000000` | Firmware/application logic; temporary compressed installer images only during the first build |
| W25Q128 QSPI | `0x90000000` | Eight native L8 backgrounds, shared CLUT, fonts and icons |
| IS42S16400J SDRAM | `0xC0000000` | Two live 480x272 L8 framebuffers |
| STM32 internal SRAM | automatic | Stack, sensor values and network response buffers |

The front and back display frames use 261,120 bytes total. The CPU finishes a
new frame in the hidden SDRAM buffer and swaps it only during LTDC vertical
blanking. The full background can therefore be rebuilt atomically with no
visible white/black flash.

## Files to copy into STM32CubeIDE

Back up the currently working CubeIDE project first.

Copy to `Core/Src`:

- `main.c`
- `qspi_assets.c`
- `weather_backgrounds.c` (first installer build only)
- `gui_modern_assets.c` (first installer build only)

Copy to `Core/Inc`:

- `qspi_assets.h`
- `weather_backgrounds.h`
- `gui_modern_assets.h`

Keep your already-working Bosch `bme68x.c` and `bme68x.h`. Do not add
`SDRAM_Diagnostic_main.c` to the application build; it contains another
`main()` and is only a separate diagnostic reference.

## Set the private values

For the recovered final application, copy `Core/Inc/app_secrets.h.example` to
the ignored local file `Core/Inc/app_secrets.h` and replace its placeholders:

```c
#define WIFI_SSID         "REPLACE_WITH_YOUR_WIFI_NAME"
#define WIFI_PASSWORD     "REPLACE_WITH_YOUR_WIFI_PASSWORD"
#define BLYNK_AUTH_TOKEN  "REPLACE_WITH_YOUR_DEVICE_TOKEN"
```

The template ID/name in the code are metadata for documentation. Raw Blynk
HTTP updates require the device authentication token, not those two template
strings.

Do not publish real Wi-Fi credentials or the Blynk device token in the course
archive, report, screenshots or public repository.

## First build: install QSPI asset version 4

The delivered `qspi_assets.h` intentionally starts with:

```c
#define QSPI_ASSET_INSTALLER  1U
```

1. Ensure `weather_backgrounds.c` and `gui_modern_assets.c` are included in
   the active Debug build (right-click each file, Resource Configurations,
   Exclude from Build, and leave Debug unchecked).
2. Project > Clean.
3. Build and flash.
4. Do not remove power while the display says the assets are being installed.
5. Wait until the display reports `QSPI ASSETS / INSTALLED` and then reaches
   the application.

The eight C source images are only 240x136 indexed pixels. During this one
build, the installer expands every pixel to a 2x2 block and writes native
480x272 frames into QSPI. This keeps the temporary firmware small enough for
the STM32F746 internal Flash.

## Second build: normal application

After the successful installer boot:

1. Change `QSPI_ASSET_INSTALLER` from `1U` to `0U` in `qspi_assets.h`.
2. Right-click `weather_backgrounds.c` > Resource Configurations > Exclude
   from Build, and check both Debug and Release.
3. Do the same for `gui_modern_assets.c`.
4. Clean, build and flash again.

Do not delete either generated file from your submitted source archive. They
are required to reproduce the QSPI image later, but they should not occupy
STM32 internal Flash in the normal build.

## QSPI version-4 map

| Region | QSPI offset |
|---|---:|
| Header | `0x000000` |
| 8 x 480x272 L8 backgrounds | `0x001000` to `0x0FFFFF` |
| Shared 256-entry CLUT | `0x100000` |
| Fallback 5x7 font | `0x101000` |
| Modern fonts/icons | `0x102000` |
| End of erased asset region | `0x104000` |

`qspi_assets.c` verifies W25Q128 JEDEC ID `EF 40 18`, version-4 metadata and
CRC32 values before entering memory-mapped mode.

## Blynk datastreams

The existing V0–V11 measurements are preserved. Add these optional
datastreams to show outside weather:

| Pin | Type | Meaning |
|---|---|---|
| V12 | Integer | Outside temperature, C |
| V13 | Integer | Feels-like temperature, C |
| V14 | Integer | Daily high, C |
| V15 | Integer | Daily low, C |
| V16 | Integer | Theme number: 0 day, 1 dawn, 2 sunset, 3 night, 4 cloudy, 5 rain, 6 storm, 7 snow |

## Network behavior

- Location/timezone: refresh every six hours; retry every five minutes.
- Weather: refresh every 15 minutes; retry every minute after a failure.
- The last valid forecast remains on screen and is marked `STALE` after
  30 minutes without an update.
- The HTTP `Date` header supplies UTC. The location response supplies the
  local UTC/DST offset, and `HAL_GetTick()` advances the clock between syncs.
- Weather is never required for local flame/gas/sensor alarms to function.

To use exact fixed coordinates instead of IP geolocation, set
`internet_weather.latitude`, `.longitude`, `.city`, `.utc_offset_seconds` and
`.location_valid = true` before the first `Weather_Fetch()`, then disable the
periodic `Location_Fetch()` block. Keep in mind that a manually fixed UTC
offset must be changed when daylight-saving time changes.

## Calmer air-quality behavior

The displayed CO2 interpretation is now:

| CO2 | Display/action |
|---:|---|
| below 1000 ppm | GOOD |
| 1000–1499 ppm | FAIR |
| 1500–2499 ppm | POOR; a short reminder beep every 30 seconds |
| 2500 ppm or higher | DANGER/critical alarm |

The BME688 gas alarm now:

- learns its heater-stable baseline for 60 valid samples (about five minutes);
- ignores resistance increases;
- triggers only below 55% of baseline for six consecutive samples
  (about 30 seconds);
- clears only above 75% for six consecutive samples.

Flame detection stays immediate and uses the fast critical buzzer pattern.
Sensor-maintenance faults now beep only once per minute.

These thresholds reduce nuisance alarms, but the BME688 is not a calibrated
combustible-gas or CO safety detector. This project remains an experimental
monitor. Keep independently powered, certified smoke, fire, carbon-monoxide
and combustible-gas alarms installed.

## CubeMX settings that must remain unchanged

Use the already proven settings:

- HCLK: 200 MHz;
- FMC SDCLK: HCLK/3 = 66.667 MHz;
- SDRAM refresh count: 1022;
- 8 column bits, 12 row bits, 16-bit bus, 4 banks, CAS 3;
- read burst disabled and read-pipe delay 0;
- HSE bypass for the powered 8 MHz ASCO oscillator;
- LTDC 480x272 timings already proven on the MDT0430A01ISC-RGB;
- I2C3/BME688, I2C4/SCD41, ADC1 IN1/PA1, PH2 flame input,
  USART1/ESP-01 and the current buzzer GPIO.

## Bench test

1. First installer boot reports `QSPI ASSETS / INSTALLED`.
2. Normal build reports `QSPI ASSETS / READY`.
3. City, local time/date and outside forecast appear after Wi-Fi connects.
4. Background changes for day, dawn, sunset, night and weather conditions.
5. Indoor values update without a whole-screen flash.
6. Disconnect the router briefly: the local sensors and alarms continue.
7. Reconnect: weather and Blynk recover automatically.
8. Exercise alarm logic electronically by simulating inputs or flags. Do not
   use dangerous gas exposure or uncontrolled flame; use certified test
   equipment and independently powered approved detectors.
9. Run the complete hub continuously for at least 30 minutes.

`Assets/previews/weather_gui_preview_sheet.png` shows four representative
no-card layouts after running `Tools/preview_weather_gui.py`.
