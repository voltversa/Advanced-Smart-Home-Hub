# Firmware

This folder contains the final recovered Smart Home Hub application sources, generated GUI/QSPI assets, asset tools, the MCP2221A service console, and memory diagnostics.

## Included

- `Core/Src/main.c` — final recovered application logic.
- `Core/Src/mcp2221a_console.c` and matching header — USART3 service console.
- `Core/Src/qspi_assets.c` — W25Q128 detection, asset installation, CRC checking, and memory-mapped access.
- `Core/Src/weather_backgrounds.c` and `gui_modern_assets.c` — first-install asset sources.
- `Core/Inc/app_config.h` — timings, thresholds, Blynk pins, and non-secret settings.
- `Core/Inc/app_secrets.h.example` — safe local-credential template.
- `Assets/` and `Tools/` — source artwork and reproducible asset-generation scripts.
- `Reference/SDRAM_Diagnostic_main.c` — separate full-capacity SDRAM diagnostic.

## Complete project and recovered-source boundary

This curated folder does not duplicate the generated HAL `Drivers/` tree, startup/linker files, `main.h`, or Bosch BME68x driver. Those dependencies are available in the sanitized complete project at [`submission/Mahmoud_Mostafa_Adv_pcb/09_Firmware_sanitized.zip`](../submission/Mahmoud_Mostafa_Adv_pcb/09_Firmware_sanitized.zip).

The complete archive's application source and `.ioc` differ from this curated final source, and its FMC snapshot does not match the final passing SDRAM settings documented in Rev 2.1. Use it as an integration base, then reconcile settings and replace application files deliberately; do not assume it is a verified one-click build.

The files under `Reference/legacy-cubemx/` are dated 4 August 2026 and are retained for pin and peripheral reference. They predate the final passing SDRAM configuration documented in Rev 2.1; review FMC settings before any code regeneration.

## Private configuration

Create the local file:

```text
Core/Inc/app_secrets.h
```

from `app_secrets.h.example`, then enter the 2.4 GHz Wi-Fi SSID/password and Blynk device token. The real file is ignored by Git and must not be committed.

The originally attached complete archive contained live credentials and compiled copies in `Debug/`. Its repository copy was sanitized by removing `app_secrets.h` and the entire debug-output tree. Rotate the exposed credentials before reuse.

## QSPI two-build workflow

1. Enable `QSPI_ASSET_INSTALLER` for the installer build.
2. Include `weather_backgrounds.c` and `gui_modern_assets.c`.
3. Flash and wait for the display to report `QSPI ASSETS / INSTALLED`.
4. Disable the installer flag.
5. Exclude the two large installer sources from normal Debug and Release builds.
6. Clean, rebuild, and flash the normal application.

Full details and the QSPI map are in `README_WEATHER_GUI_ASSETS.md`.

That longer guide is a historical snapshot of the earlier IP-geolocation/Open-Meteo revision. Its network timings, location behavior, and credential-placement instructions do not describe the recovered final `main.c`; use this README, `app_secrets.h.example`, and the current source for integration.

## Validation notes

`VALIDATION.txt` records the host-side structural and console checks performed on the recovered application. Target hardware results are documented in the Rev 2.1 report; the host checks are not a substitute for a complete CubeIDE target build.

The current source is authoritative for the relative gas-change thresholds (`55%` trigger, `75%` clear, six samples each). The report contains earlier algorithm prose. Neither source nor report establishes certified gas detection or a validated end-to-end gas-triggered life-safety response.

The final source hard-codes Brussels and a UTC+2 offset. Belgium uses UTC+1 in winter, so the displayed local time will be one hour fast outside daylight-saving time until the location/time code is corrected.
