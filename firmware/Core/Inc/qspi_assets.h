#ifndef QSPI_ASSETS_H
#define QSPI_ASSETS_H

#include "stm32f7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * This delivery starts at 1 so the first flash installs asset version 4.
 * After the display reports INSTALLED, change it to 0 and exclude
 * weather_backgrounds.c plus gui_modern_assets.c from the normal build.
 */
#ifndef QSPI_ASSET_INSTALLER
#define QSPI_ASSET_INSTALLER  1U
#endif

#define QSPI_MEMORY_MAPPED_BASE          0x90000000UL
#define QSPI_ASSET_HEADER_OFFSET         0x00000000UL
#define QSPI_ASSET_BACKGROUND_OFFSET     0x00001000UL
#define QSPI_ASSET_BACKGROUND_FRAME_BYTES (480UL * 272UL)
#define QSPI_WEATHER_BACKGROUND_COUNT    8UL
#define QSPI_ASSET_BACKGROUND_BYTES      \
    (QSPI_ASSET_BACKGROUND_FRAME_BYTES * QSPI_WEATHER_BACKGROUND_COUNT)
#define QSPI_ASSET_CLUT_OFFSET           0x00100000UL
#define QSPI_ASSET_CLUT_ENTRIES          256UL
#define QSPI_ASSET_CLUT_BYTES            \
    (QSPI_ASSET_CLUT_ENTRIES * sizeof(uint32_t))
#define QSPI_ASSET_FONT_MAP_OFFSET       0x00101000UL
#define QSPI_ASSET_MODERN_OFFSET         0x00102000UL
#define QSPI_ASSET_MODERN_BYTES          6640UL

#define QSPI_ASSET_MAGIC                 0x48554241UL /* "HUBA" */
#define QSPI_ASSET_VERSION               4UL

#define QSPI_MODERN_ICON_WIDTH           20U
#define QSPI_MODERN_ICON_HEIGHT          20U

typedef enum
{
    QSPI_FONT_SMALL = 0,
    QSPI_FONT_MEDIUM,
    QSPI_FONT_LARGE,
    QSPI_FONT_COUNT
} QSPI_FontSize;

typedef enum
{
    QSPI_ICON_TEMPERATURE = 0,
    QSPI_ICON_HUMIDITY,
    QSPI_ICON_PRESSURE,
    QSPI_ICON_CO2,
    QSPI_ICON_AIR_QUALITY,
    QSPI_ICON_WIFI,
    QSPI_ICON_CLOUD,
    QSPI_ICON_COUNT
} QSPI_Icon;

/* Must stay in the same order as weather_backgrounds.c. */
typedef enum
{
    QSPI_WEATHER_CLEAR_DAY = 0,
    QSPI_WEATHER_DAWN,
    QSPI_WEATHER_SUNSET,
    QSPI_WEATHER_NIGHT,
    QSPI_WEATHER_CLOUDY,
    QSPI_WEATHER_RAIN,
    QSPI_WEATHER_STORM,
    QSPI_WEATHER_SNOW,
    QSPI_WEATHER_COUNT
} QSPI_WeatherTheme;

typedef struct
{
    int8_t left;
    uint8_t top;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    uint8_t row_bytes;
    const uint8_t *bitmap;
} QSPI_FontGlyph;

typedef enum
{
    QSPI_ASSET_ERROR_NONE = 0,
    QSPI_ASSET_ERROR_ARGUMENT,
    QSPI_ASSET_ERROR_RESET,
    QSPI_ASSET_ERROR_JEDEC_ID,
    QSPI_ASSET_ERROR_QUAD_ENABLE,
    QSPI_ASSET_ERROR_HEADER_READ,
    QSPI_ASSET_ERROR_NOT_INSTALLED,
    QSPI_ASSET_ERROR_ERASE,
    QSPI_ASSET_ERROR_PROGRAM,
    QSPI_ASSET_ERROR_VERIFY,
    QSPI_ASSET_ERROR_MEMORY_MAP
} QSPI_AssetError;

/*
 * Initializes/validates the W25Q128 assets and finally leaves QUADSPI in
 * memory-mapped read mode. installed_now is true only after a first-time
 * programming operation.
 */
HAL_StatusTypeDef QSPI_Assets_Init(QSPI_HandleTypeDef *hqspi,
                                   bool *installed_now);

/* Valid only after QSPI_Assets_Init() returns HAL_OK. */
/* BackgroundL8 is kept for compatibility and returns CLEAR_DAY. */
const uint8_t *QSPI_Assets_BackgroundL8(void);
const uint8_t *QSPI_Assets_WeatherBackgroundL8(QSPI_WeatherTheme theme);
const uint32_t *QSPI_Assets_CLUT(void);
const uint8_t *QSPI_Assets_GetGlyph5x7(char character);

/* Proportional modern fonts and 20x20 icons stored in QSPI asset version 4. */
bool QSPI_Assets_GetModernGlyph(QSPI_FontSize size,
                                char character,
                                QSPI_FontGlyph *glyph);
uint8_t QSPI_Assets_ModernLineHeight(QSPI_FontSize size);
const uint8_t *QSPI_Assets_GetIcon(QSPI_Icon icon);

QSPI_AssetError QSPI_Assets_LastError(void);

#endif /* QSPI_ASSETS_H */
