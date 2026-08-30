# Organized-source changes

## SCD41 room-environment compensation

- Made the SCD41 the authoritative source for displayed room temperature and
  humidity.
- Configured a 9.3 °C SCD41 integration offset at every boot, derived from the
  stabilized 32.3 °C sensor output, a 27.0 °C reference, and the sensor's
  existing 4.0 °C default offset.
- Added light IIR smoothing to the displayed room temperature.
- Changed temperature and humidity alarms to use the compensated SCD41 data.
- Retained the BME688 temperature as a PCB/heater diagnostic; pressure and gas
  measurements still come from the BME688.
- Added the BME-to-room thermal delta to the MCP2221A `SENSORS` report.

## Initial organization and MCP2221A integration

- Converted the supplied Markdown-formatted listing back into valid C source.
- Preserved the working LCD, QSPI, SDRAM, SCD41, BME688, flame, buzzer,
  ESP-01, weather and Blynk application logic.
- Reserved USART1 exclusively for the ESP-01.
- Added `mcp2221a_console.c/.h` for the MCP2221A on USART3.
- Replaced scattered direct USART3 transmissions with one console API.
- Added a startup banner, boot diagnostics and two-way terminal commands.
- Added live `STATUS`, `SENSORS`, `NETWORK` and `MEMORY` reports.
- Moved timing, alarm thresholds and Blynk pin assignments into
  `app_config.h`.
- Moved private credentials into `app_secrets.h` and added a safe example.
- Fixed the malformed `+CIFSR:STAIP` search string and Markdown-escaped
  bitwise-NOT operators found in the submitted listing.
- Documented all required Blynk V0-V16 datastreams.
