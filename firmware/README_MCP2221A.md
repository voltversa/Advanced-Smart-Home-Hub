# MCP2221A debug-console integration

This source package keeps the ESP-01 on **USART1** and uses the MCP2221A-I/SL
only as the PC debug console on **USART3**.

## Hardware mapping

> **As-built caveat:** the mapping below is the intended functional interface.
> The archived compiled schematic's MCP2221A/USBLC6 section contains
> inconsistent D− and VUSB connections and does not capture a reliable
> as-built path. The prototype worked after a D+ repair; trace the physical
> board and correct the Altium source before reproducing this interface.

- STM32 `PC10 / USART3_TX` -> MCP2221A `URX`
- STM32 `PC11 / USART3_RX` <- MCP2221A `UTX`
- MCP2221A `VDD` = 3.3 V
- MCP2221A `VUSB` must follow the device datasheet and the verified board wiring; do not infer it from the archived sheet
- Common ground

The MCP2221A is a transparent USB-to-UART bridge. It does not require a
special STM32 driver.

## CubeMX USART3 configuration

- Mode: Asynchronous
- Baud rate: 115200
- Word length: 8 bits
- Parity: None
- Stop bits: 1
- Hardware flow control: None
- TX: PC10, AF7
- RX: PC11, AF7

## Files to copy into the CubeIDE project

1. Replace `Core/Src/main.c` with the supplied organized version.
2. Copy `Core/Src/mcp2221a_console.c` into `Core/Src`.
3. Copy these files into `Core/Inc`:
   - `mcp2221a_console.h`
   - `app_config.h`
   - create `app_secrets.h` from `app_secrets.h.example`
4. Refresh the CubeIDE project and run **Project -> Clean**, then **Build**.

The credential-bearing `app_secrets.h` is intentionally excluded from this
repository. Keep the local file untracked; `app_secrets.h.example` is the safe
template.

## Terminal configuration

Open the MCP2221A COM port in RealTerm, Tera Term or PuTTY using:

- 115200 baud
- 8 data bits
- No parity
- 1 stop bit
- No flow control
- ASCII display

Reset the board. A HOME HUB banner should appear.

## Commands

- `HELP` or `?` - list commands
- `PING` - test both UART directions; response must be `PONG`
- `INFO` - display the USART3/MCP2221A configuration
- `STATUS` - memory, sensor, network and alarm summary
- `SENSORS` - live SCD41, BME688, gas and flame values
- `NETWORK` - ESP-01, Wi-Fi, Blynk, location and weather state
- `MEMORY` - SDRAM and QSPI state

`SENSORS` also reports the compensated SCD41 room temperature/humidity, the
BME688 PCB-side diagnostics, and their thermal difference.

Typing `PING` manually verifies the complete path:

PC -> USB -> MCP2221A UTX -> STM32 PC11 -> firmware -> STM32 PC10 ->
MCP2221A URX -> USB -> PC.

## Blynk reminder

The firmware batch request uses virtual pins V0 through V16 when all data is
available. Every transmitted pin must exist as a datastream in the `Home HUB`
template with a compatible data type.

## Temperature compensation

The dashboard and room-temperature alarms use the SCD41. At startup the
firmware writes the configured SCD41 integration offset from
`SCD41_TEMPERATURE_OFFSET10` in `app_config.h`. The current value is 93,
meaning 9.3 °C. The BME688 temperature is not averaged into the room value
because its gas heater and PCB heat make it unsuitable as the ambient source
in this assembly.

After changing the enclosure, ventilation, display brightness or power load,
run the complete hub for at least 30 minutes beside a trusted thermometer and
recalibrate the offset under thermal equilibrium.
