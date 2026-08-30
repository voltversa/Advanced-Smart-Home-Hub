/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart-home dashboard with QSPI assets and SDRAM frames
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_config.h"
#include "bme68x.h"
#include "mcp2221a_console.h"
#include "qspi_assets.h"
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Display size */
#define LCD_WIDTH            480U
#define LCD_HEIGHT           272U
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Full-screen 8-bit indexed GUI: 480 x 272 = 130560 bytes per frame. */
#define TEXT_WIDTH           LCD_WIDTH
#define TEXT_HEIGHT          LCD_HEIGHT
#define TEXT_X               0U
#define TEXT_Y               0U

/* Palette indices used by the LTDC L8 foreground layer. */
#define LCD_BLACK            1U
#define LCD_WHITE            2U
#define LCD_RED              3U
#define LCD_GREEN            4U
#define LCD_BLUE             5U
#define LCD_CYAN             6U
#define LCD_YELLOW           7U
#define LCD_DARK_BLUE        8U
#define LCD_CARD             9U
#define LCD_CARD_ALT         10U
#define LCD_MUTED            11U
#define LCD_ORANGE           12U
#define LCD_TEAL             13U
#define LCD_WARNING_BG       14U
#define LCD_DANGER_BG        15U

/* -------------------- SDRAM -------------------- */

#define SDRAM_BASE_ADDRESS       0xC0000000UL

/*
 * Proven IS42S16400J configuration:
 * HCLK = 200 MHz, FMC SDCLK = HCLK / 3 = 66.667 MHz.
 * Refresh = (64 ms / 4096 rows) * 66.667 MHz - 20 = about 1022.
 */
#define SDRAM_REFRESH_COUNT      1022U

/* Two 480x272 L8 buffers, deliberately 256-KiB apart. */
#define LCD_FRAME_BYTES          (TEXT_WIDTH * TEXT_HEIGHT)
#define LCD_FRAMEBUFFER_0_ADDR   (SDRAM_BASE_ADDRESS + 0x00000000UL)
#define LCD_FRAMEBUFFER_1_ADDR   (SDRAM_BASE_ADDRESS + 0x00040000UL)

#define SDRAM_MODEREG_BURST_LENGTH_1          0x0000U
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   0x0000U
#define SDRAM_MODEREG_CAS_LATENCY_3            0x0030U
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD  0x0000U
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE   0x0200U
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* Frame pixels live in SDRAM, not in the STM32's internal SRAM. */
static uint8_t *text_framebuffer =
    (uint8_t *)(uintptr_t)LCD_FRAMEBUFFER_1_ADDR;
static uint8_t *lcd_front_buffer =
    (uint8_t *)(uintptr_t)LCD_FRAMEBUFFER_0_ADDR;
static uint8_t *lcd_back_buffer =
    (uint8_t *)(uintptr_t)LCD_FRAMEBUFFER_1_ADDR;
static bool lcd_double_buffer_ready = false;
static QSPI_WeatherTheme lcd_background_theme = QSPI_WEATHER_CLEAR_DAY;

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;
I2C_HandleTypeDef hi2c4;

LTDC_HandleTypeDef hltdc;

QSPI_HandleTypeDef hqspi;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN PV */
static void BME688_GasTestScreen(bool sensor_online);
static bool gui_dashboard_active = false;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_LTDC_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
static void MX_I2C4_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_FMC_Init(void);
static void MX_QUADSPI_Init(void);
/* USER CODE BEGIN PFP */
static void App_ConsoleCommand(const char *command);
static HAL_StatusTypeDef FlameSensor_ReadADC(uint16_t *raw);
static void FlameSensor_TestScreen(void);
static bool Safety_FlameActiveDuringNetwork(void);
static uint16_t ESP01_SendCommand(const char *command,
                                  char *response,
                                  uint16_t capacity,
                                  uint32_t timeout_ms);

static bool ESP01_TestScreen(void);
static bool ESP01_ConnectWiFi(void);
static uint16_t ESP01_ReadUntil(char *response,
                                uint16_t capacity,
                                const char *success,
                                const char *failure,
                                uint32_t timeout_ms);

static bool ESP01_PrepareDataSend(uint16_t data_length);

static bool Blynk_UpdateVirtualPin(uint8_t virtual_pin,
                                   const char *value);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t colour)
{
    if ((x < TEXT_WIDTH) && (y < TEXT_HEIGHT))
    {
        text_framebuffer[(y * TEXT_WIDTH) + x] = (uint8_t)colour;
    }
}

static void LCD_FillRectangle(uint16_t x,
                              uint16_t y,
                              uint16_t width,
                              uint16_t height,
                              uint16_t colour)
{
    uint16_t px;
    uint16_t py;

    for (py = 0U; py < height; py++)
    {
        for (px = 0U; px < width; px++)
        {
            LCD_DrawPixel(x + px, y + py, colour);
        }
    }
}

static void LCD_ClearTextLayer(uint16_t colour)
{
    uint32_t pixel;

    for (pixel = 0U;
         pixel < (TEXT_WIDTH * TEXT_HEIGHT);
         pixel++)
    {
        text_framebuffer[pixel] = (uint8_t)colour;
    }
}

static bool LCD_LoadBackground(void)
{
    const uint8_t *background =
        QSPI_Assets_WeatherBackgroundL8(lcd_background_theme);

    if (background == NULL)
        return false;

    memcpy(text_framebuffer, background, LCD_FRAME_BYTES);
    return true;
}

static void LCD_RestoreBackgroundRectangle(uint16_t x,
                                           uint16_t y,
                                           uint16_t width,
                                           uint16_t height)
{
    const uint8_t *background =
        QSPI_Assets_WeatherBackgroundL8(lcd_background_theme);

    if ((background == NULL) ||
        (x >= TEXT_WIDTH) ||
        (y >= TEXT_HEIGHT))
    {
        return;
    }

    if (((uint32_t)x + width) > TEXT_WIDTH)
        width = (uint16_t)(TEXT_WIDTH - x);

    if (((uint32_t)y + height) > TEXT_HEIGHT)
        height = (uint16_t)(TEXT_HEIGHT - y);

    for (uint16_t row = 0U; row < height; row++)
    {
        uint32_t offset = ((uint32_t)y + row) * TEXT_WIDTH + x;
        memcpy(&text_framebuffer[offset],
               &background[offset],
               width);
    }
}

static QSPI_FontSize LCD_FontFromScale(uint8_t scale)
{
    if (scale >= 3U)
        return QSPI_FONT_LARGE;

    if (scale == 2U)
        return QSPI_FONT_MEDIUM;

    return QSPI_FONT_SMALL;
}

static uint16_t LCD_MeasureString(const char *text, uint8_t scale)
{
    QSPI_FontGlyph glyph;
    QSPI_FontSize font = LCD_FontFromScale(scale);
    uint32_t width = 0U;

    if (text == NULL)
        return 0U;

    while (*text != '\0')
    {
        if (QSPI_Assets_GetModernGlyph(font, *text, &glyph))
            width += (uint32_t)glyph.advance + 1U;

        text++;
    }

    if (width != 0U)
        width--;

    return (width > 0xFFFFU) ? 0xFFFFU : (uint16_t)width;
}

static uint8_t LCD_DrawCharacter(uint16_t x,
                                 uint16_t y,
                                 char character,
                                 uint16_t colour,
                                 uint8_t scale)
{
    QSPI_FontGlyph glyph;
    QSPI_FontSize font = LCD_FontFromScale(scale);
    uint8_t row;
    uint8_t column;

    if (!QSPI_Assets_GetModernGlyph(font, character, &glyph))
        return 0U;

    for (row = 0U; row < glyph.height; row++)
    {
        for (column = 0U; column < glyph.width; column++)
        {
            uint8_t pixel = glyph.bitmap[
                ((uint32_t)row * glyph.row_bytes) + (column >> 3U)
            ];

            if ((pixel & (uint8_t)(0x80U >> (column & 7U))) != 0U)
            {
                int32_t pixel_x = (int32_t)x + glyph.left + column;
                int32_t pixel_y = (int32_t)y + glyph.top + row;

                if ((pixel_x >= 0) && (pixel_y >= 0) &&
                    (pixel_x < (int32_t)TEXT_WIDTH) &&
                    (pixel_y < (int32_t)TEXT_HEIGHT))
                {
                    LCD_DrawPixel((uint16_t)pixel_x,
                                  (uint16_t)pixel_y,
                                  colour);
                }
            }
        }
    }

    return glyph.advance;
}

static void LCD_DrawString(uint16_t x,
                           uint16_t y,
                           const char *text,
                           uint16_t colour,
                           uint8_t scale)
{
    while (*text != '\0')
    {
        uint8_t advance = LCD_DrawCharacter(x, y, *text, colour, scale);

        x += (uint16_t)advance + 1U;
        text++;

        if (x >= TEXT_WIDTH)
        {
            break;
        }
    }
}

static void LCD_DrawCenteredString(uint16_t y,
                                   const char *text,
                                   uint16_t colour,
                                   uint8_t scale)
{
    uint16_t width = LCD_MeasureString(text, scale);
    uint16_t x = 0U;

    if (width < TEXT_WIDTH)
    {
        x = (uint16_t)((TEXT_WIDTH - width) / 2U);
    }

    LCD_DrawString(x, y, text, colour, scale);
}

static uint16_t LCD_MeasureStringZoom(const char *text,
                                      QSPI_FontSize font,
                                      uint8_t zoom)
{
    QSPI_FontGlyph glyph;
    uint32_t width = 0U;

    if ((text == NULL) || (zoom == 0U))
        return 0U;

    while (*text != '\0')
    {
        if (QSPI_Assets_GetModernGlyph(font, *text, &glyph))
            width += ((uint32_t)glyph.advance + 1U) * zoom;

        text++;
    }

    if (width >= zoom)
        width -= zoom;

    return (width > 0xFFFFU) ? 0xFFFFU : (uint16_t)width;
}

static uint16_t LCD_DrawCharacterZoom(uint16_t x,
                                      uint16_t y,
                                      char character,
                                      uint16_t colour,
                                      QSPI_FontSize font,
                                      uint8_t zoom)
{
    QSPI_FontGlyph glyph;

    if ((zoom == 0U) ||
        !QSPI_Assets_GetModernGlyph(font, character, &glyph))
    {
        return 0U;
    }

    for (uint8_t row = 0U; row < glyph.height; row++)
    {
        for (uint8_t column = 0U; column < glyph.width; column++)
        {
            uint8_t pixel = glyph.bitmap[
                ((uint32_t)row * glyph.row_bytes) + (column >> 3U)
            ];

            if ((pixel & (uint8_t)(0x80U >> (column & 7U))) != 0U)
            {
                int32_t pixel_x = (int32_t)x +
                    ((int32_t)glyph.left + column) * zoom;
                int32_t pixel_y = (int32_t)y +
                    ((int32_t)glyph.top + row) * zoom;

                for (uint8_t zy = 0U; zy < zoom; zy++)
                {
                    for (uint8_t zx = 0U; zx < zoom; zx++)
                    {
                        int32_t draw_x = pixel_x + zx;
                        int32_t draw_y = pixel_y + zy;

                        if ((draw_x >= 0) && (draw_y >= 0) &&
                            (draw_x < (int32_t)TEXT_WIDTH) &&
                            (draw_y < (int32_t)TEXT_HEIGHT))
                        {
                            LCD_DrawPixel((uint16_t)draw_x,
                                          (uint16_t)draw_y,
                                          colour);
                        }
                    }
                }
            }
        }
    }

    return (uint16_t)glyph.advance * zoom;
}

static void LCD_DrawStringZoom(uint16_t x,
                               uint16_t y,
                               const char *text,
                               uint16_t colour,
                               QSPI_FontSize font,
                               uint8_t zoom)
{
    if (text == NULL)
        return;

    while (*text != '\0')
    {
        uint16_t advance = LCD_DrawCharacterZoom(x,
                                                 y,
                                                 *text,
                                                 colour,
                                                 font,
                                                 zoom);
        x += advance + zoom;
        text++;

        if (x >= TEXT_WIDTH)
            break;
    }
}

static void LCD_DrawCenteredStringShadow(uint16_t y,
                                         const char *text,
                                         uint16_t colour,
                                         uint8_t scale)
{
    uint16_t width = LCD_MeasureString(text, scale);
    uint16_t x = (width < TEXT_WIDTH) ?
        (uint16_t)((TEXT_WIDTH - width) / 2U) : 0U;

    LCD_DrawString(x + 1U, y + 1U, text, LCD_BLACK, scale);
    LCD_DrawString(x, y, text, colour, scale);
}

static void LCD_DrawStringShadow(uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint16_t colour,
                                 uint8_t scale)
{
    LCD_DrawString(x + 1U, y + 1U, text, LCD_BLACK, scale);
    LCD_DrawString(x, y, text, colour, scale);
}

static void LCD_DrawRightStringShadow(uint16_t right,
                                      uint16_t y,
                                      const char *text,
                                      uint16_t colour,
                                      uint8_t scale)
{
    uint16_t width = LCD_MeasureString(text, scale);
    uint16_t x = (width < right) ? (uint16_t)(right - width) : 0U;

    LCD_DrawStringShadow(x, y, text, colour, scale);
}

static void LCD_DrawCenteredZoomShadow(uint16_t y,
                                       const char *text,
                                       uint16_t colour,
                                       QSPI_FontSize font,
                                       uint8_t zoom)
{
    uint16_t width = LCD_MeasureStringZoom(text, font, zoom);
    uint16_t x = (width < TEXT_WIDTH) ?
        (uint16_t)((TEXT_WIDTH - width) / 2U) : 0U;

    LCD_DrawStringZoom(x + 2U, y + 2U, text, LCD_BLACK, font, zoom);
    LCD_DrawStringZoom(x, y, text, colour, font, zoom);
}

static void LCD_DrawIcon(uint16_t x,
                         uint16_t y,
                         QSPI_Icon icon,
                         uint16_t colour)
{
    const uint8_t *bitmap = QSPI_Assets_GetIcon(icon);
    const uint8_t row_bytes = (QSPI_MODERN_ICON_WIDTH + 7U) / 8U;

    if (bitmap == NULL)
        return;

    for (uint8_t row = 0U; row < QSPI_MODERN_ICON_HEIGHT; row++)
    {
        for (uint8_t column = 0U; column < QSPI_MODERN_ICON_WIDTH; column++)
        {
            uint8_t pixel = bitmap[((uint32_t)row * row_bytes) +
                                   (column >> 3U)];
            if ((pixel & (uint8_t)(0x80U >> (column & 7U))) != 0U)
            {
                LCD_DrawPixel(x + column, y + row, colour);
            }
        }
    }
}

static void LCD_PresentTextLayer(void)
{
    uint8_t *old_front;
    uint32_t reload_start;

    if (!lcd_double_buffer_ready)
        return;

    /*
     * The SDRAM MPU region is deliberately non-cacheable, so no cache clean
     * is required. Point layer 1 at the completed back buffer and request a
     * vertical-blank reload. LTDC keeps displaying the old front buffer until
     * the next frame boundary, preventing tearing and full-screen flashing.
     */
    __DSB();

    LTDC_Layer2->CFBAR = (uint32_t)lcd_back_buffer;
    LTDC->SRCR = LTDC_SRCR_VBR;

    /* Wait for the reload, but never let a stopped LTDC hang the whole hub. */
    reload_start = HAL_GetTick();
    while ((LTDC->SRCR & LTDC_SRCR_VBR) != 0U)
    {
        if ((uint32_t)(HAL_GetTick() - reload_start) > 50U)
            Error_Handler();
    }

    old_front = lcd_front_buffer;
    lcd_front_buffer = lcd_back_buffer;
    lcd_back_buffer = old_front;
    text_framebuffer = lcd_back_buffer;

    /* The next incremental update starts from the exact image now visible. */
    memcpy(lcd_back_buffer, lcd_front_buffer, LCD_FRAME_BYTES);

    /* The display changed buffers, so all cached GUI pixels belong to both
     * framebuffers after the copy above and remain valid. */
}

static void LCD_ConfigureTextLayer(void)
{
    LTDC_LayerCfgTypeDef overlay = {0};
    const uint32_t *clut = QSPI_Assets_CLUT();

    if (clut == NULL)
        Error_Handler();

    /* Prepare both SDRAM frames before LTDC is allowed to scan either one. */
    memset(lcd_front_buffer, LCD_BLACK, LCD_FRAME_BYTES);
    memset(lcd_back_buffer, LCD_BLACK, LCD_FRAME_BYTES);
    text_framebuffer = lcd_back_buffer;

    /* LTDC scans an L8 framebuffer in external SDRAM. QSPI remains the
     * permanent asset store and is only read by the CPU. */
    overlay.WindowX0 = TEXT_X;
    overlay.WindowX1 = TEXT_X + TEXT_WIDTH;
    overlay.WindowY0 = TEXT_Y;
    overlay.WindowY1 = TEXT_Y + TEXT_HEIGHT;
    overlay.PixelFormat = LTDC_PIXEL_FORMAT_L8;
    overlay.FBStartAdress = (uint32_t)lcd_front_buffer;
    overlay.Alpha = 255U;
    overlay.Alpha0 = 255U;
    overlay.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    overlay.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    overlay.ImageWidth = TEXT_WIDTH;
    overlay.ImageHeight = TEXT_HEIGHT;

    if (HAL_LTDC_ConfigLayer(&hltdc, &overlay, 1U) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_LTDC_ConfigCLUT(&hltdc,
                            (uint32_t *)(uintptr_t)clut,
                            QSPI_ASSET_CLUT_ENTRIES,
                            1U) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_LTDC_EnableCLUT(&hltdc, 1U) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LTDC_LAYER_DISABLE(&hltdc, 0U);
    __HAL_LTDC_LAYER_ENABLE(&hltdc, 1U);
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);

    lcd_double_buffer_ready = true;
}

static APP_UNUSED void LCD_TextDemo(void)
{
    LCD_ConfigureTextLayer();
    LCD_ClearTextLayer(LCD_DARK_BLUE);

    LCD_DrawCenteredString(
        4U,
        "HELLO!",
        LCD_WHITE,
        3U
    );

    LCD_DrawCenteredString(
        36U,
        "STM32F746 WORKS!",
        LCD_CYAN,
        3U
    );

    LCD_PresentTextLayer();
}
/* =========================================================
 * Sensor results
 * Values ending with 10 have one decimal digit:
 * 235 = 23.5
 * ========================================================= */
typedef struct
{
    uint16_t scd_co2;
    int32_t  scd_temp10;
    uint32_t scd_rh10;

    /* Compensated and smoothed room values derived from the SCD41. */
    int32_t  room_temp10;
    uint32_t room_rh10;
    bool     room_environment_valid;

    int32_t  bme_temp10;
    uint32_t bme_rh10;
    uint32_t bme_pressure_hpa10;

    uint32_t bme_gas_ohms;
    bool     bme_gas_valid;
    bool     bme_heater_stable;

    uint32_t bme_gas_baseline_ohms;
    uint32_t bme_gas_ratio_percent;
    bool     bme_gas_baseline_ready;
    bool     bme_gas_alarm;

    uint16_t flame_adc_raw;
    uint32_t flame_voltage_mv;
    bool     flame_detected;
} SensorValues;

static SensorValues sensor_values;

/* Internet data is intentionally compact: wttr.in returns one short,
 * pipe-separated line and ip-api supplies the approximate public-IP city,
 * coordinates and current UTC offset.  No API key is required. */
typedef struct
{
    bool location_valid;
    bool weather_valid;
    bool clock_valid;

    char city[32];
    char latitude[16];
    char longitude[16];
    char timezone[40];
    char condition[40];

    int16_t temperature_c;
    int16_t feels_like_c;
    int16_t high_c;
    int16_t low_c;

    uint16_t dawn_minutes;
    uint16_t sunrise_minutes;
    uint16_t sunset_minutes;
    uint16_t dusk_minutes;

    int32_t utc_offset_seconds;
    uint32_t utc_epoch_at_sync;
    uint32_t sync_tick;
    uint32_t last_weather_success_tick;
    QSPI_WeatherTheme theme;
} InternetWeather;

static InternetWeather internet_weather =
{
    .city = "MY LOCATION",
    .condition = "WEATHER WAITING",
    .dawn_minutes = 0xFFFFU,
    .sunrise_minutes = 0xFFFFU,
    .sunset_minutes = 0xFFFFU,
    .dusk_minutes = 0xFFFFU,
    .theme = QSPI_WEATHER_CLEAR_DAY
};

/* =========================================================
 * SCD41 - I2C4
 * ========================================================= */

#define SCD41_ADDRESS_HAL       (0x62U << 1)
#define SCD41_START_PERIODIC    0x21B1U
#define SCD41_STOP_PERIODIC     0x3F86U
#define SCD41_DATA_READY        0xE4B8U
#define SCD41_READ_MEASUREMENT  0xEC05U
#define SCD41_SET_TEMP_OFFSET   0x241DU

static uint8_t SCD41_CRC8(const uint8_t *data)
{
    uint8_t crc = 0xFF;

    for (uint8_t byte = 0; byte < 2; byte++)
    {
        crc ^= data[byte];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }

    return crc;
}

static HAL_StatusTypeDef SCD41_SendCommand(uint16_t command)
{
    uint8_t tx[2];

    tx[0] = (uint8_t)(command >> 8);
    tx[1] = (uint8_t)(command & 0xFF);

    return HAL_I2C_Master_Transmit(&hi2c4,
                                   SCD41_ADDRESS_HAL,
                                   tx,
                                   sizeof(tx),
                                   100);
}

static HAL_StatusTypeDef SCD41_WriteWordCommand(uint16_t command,
                                                uint16_t value)
{
    uint8_t tx[5];

    tx[0] = (uint8_t)(command >> 8);
    tx[1] = (uint8_t)(command & 0xFFU);
    tx[2] = (uint8_t)(value >> 8);
    tx[3] = (uint8_t)(value & 0xFFU);
    tx[4] = SCD41_CRC8(&tx[2]);

    return HAL_I2C_Master_Transmit(&hi2c4,
                                   SCD41_ADDRESS_HAL,
                                   tx,
                                   sizeof(tx),
                                   100U);
}

static HAL_StatusTypeDef SCD41_SetTemperatureOffset10(uint16_t offset10)
{
    uint32_t raw_offset;

    /* Datasheet conversion: raw = offset_degC * 65535 / 175. */
    if (offset10 > 200U)
        return HAL_ERROR;

    raw_offset =
        (((uint32_t)offset10 * 65535U) + 875U) / 1750U;

    return SCD41_WriteWordCommand(SCD41_SET_TEMP_OFFSET,
                                  (uint16_t)raw_offset);
}

static void RoomEnvironment_Update(SensorValues *values)
{
    int32_t delta;

    if (values == NULL)
        return;

    if (!values->room_environment_valid)
    {
        values->room_temp10 = values->scd_temp10;
        values->room_rh10 = values->scd_rh10;
        values->room_environment_valid = true;
        return;
    }

    /* Smooth temperature without using the thermally biased BME688 value. */
    delta = values->scd_temp10 - values->room_temp10;

    if (delta > 0)
    {
        values->room_temp10 +=
            (delta + ROOM_TEMPERATURE_FILTER_DIV - 1) /
            ROOM_TEMPERATURE_FILTER_DIV;
    }
    else if (delta < 0)
    {
        values->room_temp10 +=
            (delta - ROOM_TEMPERATURE_FILTER_DIV + 1) /
            ROOM_TEMPERATURE_FILTER_DIV;
    }

    /* RH already uses the SCD41's configured on-chip temperature offset. */
    values->room_rh10 = values->scd_rh10;
}

static bool SCD41_IsDataReady(void)
{
    uint8_t rx[3];
    uint16_t status;

    if (SCD41_SendCommand(SCD41_DATA_READY) != HAL_OK)
        return false;

    HAL_Delay(2);

    if (HAL_I2C_Master_Receive(&hi2c4,
                               SCD41_ADDRESS_HAL,
                               rx,
                               sizeof(rx),
                               100) != HAL_OK)
    {
        return false;
    }

    if (SCD41_CRC8(rx) != rx[2])
        return false;

    status = ((uint16_t)rx[0] << 8) | rx[1];

    return (status & 0x07FFU) != 0;
}

static bool SCD41_Init(void)
{
    if (HAL_I2C_IsDeviceReady(&hi2c4,
                              SCD41_ADDRESS_HAL,
                              3,
                              100) != HAL_OK)
    {
        return false;
    }

    /*
     * Stop any measurement that may still be running after
     * an MCU-only reset. Ignore the result if already idle.
     */
    (void)SCD41_SendCommand(SCD41_STOP_PERIODIC);
    HAL_Delay(500);

    /* Configure the absolute integration offset while the SCD41 is idle. */
    if (SCD41_SetTemperatureOffset10(
            SCD41_TEMPERATURE_OFFSET10) != HAL_OK)
    {
        return false;
    }

    HAL_Delay(2U);

    if (SCD41_SendCommand(SCD41_START_PERIODIC) != HAL_OK)
        return false;

    /* First periodic result requires approximately 5 seconds. */
    HAL_Delay(5100);

    return true;
}

static bool SCD41_Read(SensorValues *values)
{
    uint8_t rx[9];
    uint16_t raw_co2;
    uint16_t raw_temp;
    uint16_t raw_rh;

    /* Wait up to one second if the new result is slightly late. */
    bool ready = false;

    for (uint8_t attempt = 0; attempt < 20; attempt++)
    {
        if (SCD41_IsDataReady())
        {
            ready = true;
            break;
        }

        HAL_Delay(50);
    }

    if (!ready)
        return false;

    if (SCD41_SendCommand(SCD41_READ_MEASUREMENT) != HAL_OK)
        return false;

    HAL_Delay(2);

    if (HAL_I2C_Master_Receive(&hi2c4,
                               SCD41_ADDRESS_HAL,
                               rx,
                               sizeof(rx),
                               100) != HAL_OK)
    {
        return false;
    }

    if ((SCD41_CRC8(&rx[0]) != rx[2]) ||
        (SCD41_CRC8(&rx[3]) != rx[5]) ||
        (SCD41_CRC8(&rx[6]) != rx[8]))
    {
        return false;
    }

    raw_co2  = ((uint16_t)rx[0] << 8) | rx[1];
    raw_temp = ((uint16_t)rx[3] << 8) | rx[4];
    raw_rh   = ((uint16_t)rx[6] << 8) | rx[7];

    values->scd_co2 = raw_co2;

    /* Temperature in 0.1 °C */
    values->scd_temp10 =
        -450 + (int32_t)(((uint32_t)1750 * raw_temp + 32767U) / 65535U);

    /* Relative humidity in 0.1 %RH */
    values->scd_rh10 =
        ((uint32_t)1000 * raw_rh + 32767U) / 65535U;

    RoomEnvironment_Update(values);

    return true;
}

/* =========================================================
 * BME688 - I2C3
 * ========================================================= */

typedef struct
{
    I2C_HandleTypeDef *i2c;
    uint8_t address;
} BME688_Bus;

static BME688_Bus bme_bus;
static struct bme68x_dev bme_device;
static struct bme68x_conf bme_config;
static struct bme68x_heatr_conf bme_heater;

static int8_t BME688_I2C_Read(uint8_t reg,
                             uint8_t *data,
                             uint32_t length,
                             void *interface_ptr)
{
    BME688_Bus *bus = (BME688_Bus *)interface_ptr;

    HAL_StatusTypeDef result =
        HAL_I2C_Mem_Read(bus->i2c,
                         bus->address << 1,
                         reg,
                         I2C_MEMADD_SIZE_8BIT,
                         data,
                         (uint16_t)length,
                         100);

    return (result == HAL_OK) ? 0 : -1;
}

static int8_t BME688_I2C_Write(uint8_t reg,
                              const uint8_t *data,
                              uint32_t length,
                              void *interface_ptr)
{
    BME688_Bus *bus = (BME688_Bus *)interface_ptr;

    HAL_StatusTypeDef result =
        HAL_I2C_Mem_Write(bus->i2c,
                          bus->address << 1,
                          reg,
                          I2C_MEMADD_SIZE_8BIT,
                          (uint8_t *)data,
                          (uint16_t)length,
                          100);

    return (result == HAL_OK) ? 0 : -1;
}

static void BME688_DelayUs(uint32_t period_us, void *interface_ptr)
{
    (void)interface_ptr;

    if (period_us != 0)
        HAL_Delay((period_us + 999U) / 1000U);
}

static bool BME688_Init(void)
{
    memset(&bme_device, 0, sizeof(bme_device));
    memset(&bme_config, 0, sizeof(bme_config));
    memset(&bme_heater, 0, sizeof(bme_heater));

    bme_bus.i2c = &hi2c3;

    /*
     * The address is 0x76 when SDO is low and 0x77 when SDO
     * is high. Your schematic should normally produce 0x77,
     * but checking both makes the test more robust.
     */
    if (HAL_I2C_IsDeviceReady(&hi2c3, 0x76U << 1, 3, 100) == HAL_OK)
    {
        bme_bus.address = 0x76;
    }
    else if (HAL_I2C_IsDeviceReady(&hi2c3, 0x77U << 1, 3, 100) == HAL_OK)
    {
        bme_bus.address = 0x77;
    }
    else
    {
        return false;
    }

    bme_device.intf = BME68X_I2C_INTF;
    bme_device.read = BME688_I2C_Read;
    bme_device.write = BME688_I2C_Write;
    bme_device.delay_us = BME688_DelayUs;
    bme_device.intf_ptr = &bme_bus;
    bme_device.amb_temp = 25;

    if (bme68x_init(&bme_device) != BME68X_OK)
        return false;

    bme_config.filter = BME68X_FILTER_OFF;
    bme_config.odr = BME68X_ODR_NONE;
    bme_config.os_hum = BME68X_OS_16X;
    bme_config.os_pres = BME68X_OS_4X;
    bme_config.os_temp = BME68X_OS_2X;

    if (bme68x_set_conf(&bme_config, &bme_device) != BME68X_OK)
        return false;

    /*
     * Bosch's standard forced-mode test:
     * heat to 300 °C for 100 ms.
     */
    bme_heater.enable = BME68X_ENABLE;
    bme_heater.heatr_temp = 300;
    bme_heater.heatr_dur = 100;
    if (bme68x_set_heatr_conf(BME68X_FORCED_MODE,
                              &bme_heater,
                              &bme_device) != BME68X_OK)
    {
        return false;
    }

    return true;
}

static bool BME688_Read(SensorValues *values)
{
    struct bme68x_data data;
    uint8_t number_of_fields = 0;
    uint32_t delay_us;

    if (bme68x_set_op_mode(BME68X_FORCED_MODE,
                           &bme_device) != BME68X_OK)
    {
        return false;
    }

    delay_us =
        bme68x_get_meas_dur(BME68X_FORCED_MODE,
                            &bme_config,
                            &bme_device) +
        ((uint32_t)bme_heater.heatr_dur * 1000UL);

    bme_device.delay_us(delay_us, bme_device.intf_ptr);

    if (bme68x_get_data(BME68X_FORCED_MODE,
                        &data,
                        &number_of_fields,
                        &bme_device) != BME68X_OK)
    {
        return false;
    }

    if (number_of_fields == 0)
        return false;

#ifdef BME68X_USE_FPU

    values->bme_temp10 =
        (int32_t)(data.temperature * 10.0f);

    values->bme_rh10 =
        (uint32_t)(data.humidity * 10.0f);

    values->bme_pressure_hpa10 =
        (uint32_t)(data.pressure / 10.0f);

#else

    /* Integer driver: temperature is °C x100. */
    if (data.temperature >= 0)
        values->bme_temp10 = (data.temperature + 5) / 10;
    else
        values->bme_temp10 = (data.temperature - 5) / 10;

    /* Humidity is %RH x1000. */
    values->bme_rh10 = (data.humidity + 50U) / 100U;

    /* Pressure is Pa. Convert to hPa x10. */
    values->bme_pressure_hpa10 = (data.pressure + 5U) / 10U;

#endif

    /* These fields are available in both FPU and integer driver builds. */
    values->bme_gas_ohms = (uint32_t)data.gas_resistance;
    values->bme_gas_valid =
        ((data.status & BME68X_GASM_VALID_MSK) != 0U);
    values->bme_heater_stable =
        ((data.status & BME68X_HEAT_STAB_MSK) != 0U);

    return true;
}

/* =========================================================
 * LCD output
 * ========================================================= */

typedef enum
{
    AIR_QUALITY_UNKNOWN = 0,
    AIR_QUALITY_GOOD,
    AIR_QUALITY_FAIR,
    AIR_QUALITY_POOR,
    AIR_QUALITY_DANGER
} AirQualityLevel;

typedef enum
{
    ALARM_NONE = 0,
    ALARM_SCD41_FAULT,
    ALARM_BME688_FAULT,
    ALARM_FLAME_SENSOR_FAULT,
    ALARM_PRESSURE_INVALID,
    ALARM_TEMPERATURE_LOW,
    ALARM_TEMPERATURE_HIGH,
    ALARM_HUMIDITY_LOW,
    ALARM_HUMIDITY_HIGH,
    ALARM_CO2_HIGH,
    ALARM_CO2_DANGER,
    ALARM_AIR_CHANGE,
    ALARM_FLAME
} AlarmType;

typedef enum
{
    BUZZER_OFF = 0,
    BUZZER_FAULT,
    BUZZER_WARNING,
    BUZZER_CRITICAL
} BuzzerPattern;

typedef struct
{
    AlarmType type;
    BuzzerPattern buzzer;
} SystemAlarm;

/* Small read-only snapshot exposed through the MCP2221A debug console. */
typedef struct
{
    bool sdram_ok;
    bool qspi_ok;
    bool esp_ok;
    bool wifi_ok;
    bool blynk_ok;
    bool scd41_ok;
    bool bme688_ok;
    bool flame_sensor_ok;
    AlarmType alarm;
} AppRuntimeState;

static AppRuntimeState app_runtime;

/* Screen-state cache. The static dashboard is drawn only once; afterwards
 * only value rectangles whose contents changed are repainted. */
static bool gui_dashboard_drawn = false;
static bool gui_dashboard_cache_valid = false;
static SensorValues gui_dashboard_cache;
static bool gui_last_scd_ok = false;
static bool gui_last_bme_ok = false;
static bool gui_last_wifi_ok = false;
static bool gui_last_blynk_ok = false;

static bool gui_alarm_drawn = false;
static AlarmType gui_last_alarm = ALARM_NONE;
static bool gui_last_alarm_background = false;
static char gui_last_alarm_value_line[40] = "";

static void GUI_DrawRectangle(uint16_t x,
                              uint16_t y,
                              uint16_t width,
                              uint16_t height,
                              uint16_t colour)
{
    if ((width < 2U) || (height < 2U))
        return;

    LCD_FillRectangle(x, y, width, 2U, colour);
    LCD_FillRectangle(x, y + height - 2U, width, 2U, colour);
    LCD_FillRectangle(x, y, 2U, height, colour);
    LCD_FillRectangle(x + width - 2U, y, 2U, height, colour);
}

static void GUI_DrawCard(uint16_t x,
                         uint16_t y,
                         uint16_t width,
                         uint16_t height,
                         uint16_t fill,
                         uint16_t accent)
{
    LCD_FillRectangle(x + 3U, y, width - 6U, height, fill);
    LCD_FillRectangle(x, y + 3U, width, height - 6U, fill);

    GUI_DrawRectangle(x, y, width, height, accent);
    LCD_FillRectangle(x + 8U, y + 8U, 3U, 12U, accent);
}

static void GUI_DrawCenteredInBox(uint16_t x,
                                  uint16_t width,
                                  uint16_t y,
                                  const char *text,
                                  uint16_t colour,
                                  uint8_t scale)
{
    uint16_t text_width = LCD_MeasureString(text, scale);
    uint16_t text_x = x;

    if (text_width < width)
        text_x = x + (uint16_t)((width - text_width) / 2U);

    LCD_DrawString(text_x, y, text, colour, scale);
}

static void GUI_FormatSignedTenths(char *buffer,
                                   size_t capacity,
                                   int32_t value10,
                                   const char *unit)
{
    uint32_t absolute;

    if (value10 < 0)
        absolute = (uint32_t)(-(int64_t)value10);
    else
        absolute = (uint32_t)value10;

    snprintf(buffer,
             capacity,
             "%s%lu.%lu %s",
             (value10 < 0) ? "-" : "",
             (unsigned long)(absolute / 10U),
             (unsigned long)(absolute % 10U),
             unit);
}

static AirQualityLevel AirQuality_Get(bool scd_ok,
                                      bool bme_ok,
                                      const SensorValues *values)
{
    if (!scd_ok || !bme_ok || (values == NULL))
        return AIR_QUALITY_UNKNOWN;

    if (values->flame_detected ||
        values->bme_gas_alarm ||
        (values->scd_co2 >= CO2_DANGER_PPM))
    {
        return AIR_QUALITY_DANGER;
    }

    if (values->scd_co2 >= CO2_POOR_PPM)
        return AIR_QUALITY_POOR;

    if (values->scd_co2 >= CO2_FAIR_PPM)
        return AIR_QUALITY_FAIR;

    return AIR_QUALITY_GOOD;
}

static const char *AirQuality_Name(AirQualityLevel level)
{
    switch (level)
    {
        case AIR_QUALITY_GOOD:   return "GOOD";
        case AIR_QUALITY_FAIR:   return "FAIR";
        case AIR_QUALITY_POOR:   return "POOR";
        case AIR_QUALITY_DANGER: return "DANGER";
        default:                 return "UNKNOWN";
    }
}

static const char *AirQuality_Explanation(AirQualityLevel level,
                                          const SensorValues *values)
{
    /* CO2 guidance always has priority over the VOC warm-up message. */
    if ((level == AIR_QUALITY_GOOD) &&
        (values != NULL) &&
        (!values->bme_gas_baseline_ready ||
         !values->bme_heater_stable))
    {
        return "CO2 CHECKED - VOC LEARNING";
    }

    switch (level)
    {
        case AIR_QUALITY_GOOD:
            return "CLEAN AIR - CO2 BELOW 1000 PPM";

        case AIR_QUALITY_FAIR:
            return "FAIR - VENTILATE WHEN CONVENIENT";

        case AIR_QUALITY_POOR:
            return "POOR AIR - OPEN A WINDOW";

        case AIR_QUALITY_DANGER:
            return "DANGER - LEAVE AND VENTILATE";

        default:
            return "WAITING FOR AIR SENSORS";
    }
}

static uint16_t AirQuality_Colour(AirQualityLevel level)
{
    switch (level)
    {
        case AIR_QUALITY_GOOD:   return LCD_GREEN;
        case AIR_QUALITY_FAIR:   return LCD_YELLOW;
        case AIR_QUALITY_POOR:   return LCD_ORANGE;
        case AIR_QUALITY_DANGER: return LCD_RED;
        default:                 return LCD_MUTED;
    }
}

static bool App_CommandEquals(const char *left, const char *right)
{
    if ((left == NULL) || (right == NULL))
        return false;

    while ((*left != '\0') && (*right != '\0'))
    {
        if (toupper((unsigned char)*left) !=
            toupper((unsigned char)*right))
        {
            return false;
        }

        left++;
        right++;
    }

    return (*left == '\0') && (*right == '\0');
}

static const char *App_AlarmName(AlarmType alarm)
{
    switch (alarm)
    {
        case ALARM_NONE:                 return "NONE";
        case ALARM_SCD41_FAULT:          return "SCD41 FAULT";
        case ALARM_BME688_FAULT:         return "BME688 FAULT";
        case ALARM_FLAME_SENSOR_FAULT:   return "FLAME SENSOR FAULT";
        case ALARM_PRESSURE_INVALID:     return "PRESSURE INVALID";
        case ALARM_TEMPERATURE_LOW:      return "TEMPERATURE LOW";
        case ALARM_TEMPERATURE_HIGH:     return "TEMPERATURE HIGH";
        case ALARM_HUMIDITY_LOW:         return "HUMIDITY LOW";
        case ALARM_HUMIDITY_HIGH:        return "HUMIDITY HIGH";
        case ALARM_CO2_HIGH:             return "CO2 HIGH";
        case ALARM_CO2_DANGER:           return "CO2 DANGER";
        case ALARM_AIR_CHANGE:           return "VOC/GAS CHANGE";
        case ALARM_FLAME:                return "FLAME";
        default:                         return "UNKNOWN";
    }
}

static const char *App_OnOff(bool value)
{
    return value ? "ON" : "OFF";
}

static void App_ConsoleCommand(const char *command)
{
    if (App_CommandEquals(command, "STATUS"))
    {
        MCP2221A_Console_Printf(
            "\r\nHOME HUB STATUS | uptime=%lu ms\r\n"
            "Memory : SDRAM=%s QSPI=%s\r\n"
            "Sensors: SCD41=%s BME688=%s FLAME=%s\r\n"
            "Network: ESP01=%s WIFI=%s BLYNK=%s\r\n"
            "Alarm  : %s\r\n",
            (unsigned long)HAL_GetTick(),
            App_OnOff(app_runtime.sdram_ok),
            App_OnOff(app_runtime.qspi_ok),
            App_OnOff(app_runtime.scd41_ok),
            App_OnOff(app_runtime.bme688_ok),
            App_OnOff(app_runtime.flame_sensor_ok),
            App_OnOff(app_runtime.esp_ok),
            App_OnOff(app_runtime.wifi_ok),
            App_OnOff(app_runtime.blynk_ok),
            App_AlarmName(app_runtime.alarm));
    }
    else if (App_CommandEquals(command, "SENSORS"))
    {
        MCP2221A_Console_Printf(
            "\r\nROOM (SCD41 compensated): T=%ld.%01lu C RH=%lu.%01lu %%\r\n"
            "SCD41: CO2=%u ppm T=%ld.%01lu C RH=%lu.%01lu %%\r\n"
            "BME688 PCB diagnostic: T=%ld.%01lu C RH=%lu.%01lu %% P=%lu.%01lu hPa\r\n"
            "Thermal delta BME-room: %ld.%01lu C\r\n"
            "Gas: %lu ohm ratio=%lu %% alarm=%s valid=%s stable=%s\r\n"
            "Flame: D0=%s ADC=%u voltage=%lu mV\r\n",
            (long)(sensor_values.room_temp10 / 10),
            (unsigned long)((sensor_values.room_temp10 < 0) ?
                (uint32_t)(-(int64_t)sensor_values.room_temp10) % 10U :
                (uint32_t)sensor_values.room_temp10 % 10U),
            (unsigned long)(sensor_values.room_rh10 / 10U),
            (unsigned long)(sensor_values.room_rh10 % 10U),
            (unsigned int)sensor_values.scd_co2,
            (long)(sensor_values.scd_temp10 / 10),
            (unsigned long)((sensor_values.scd_temp10 < 0) ?
                (uint32_t)(-(int64_t)sensor_values.scd_temp10) % 10U :
                (uint32_t)sensor_values.scd_temp10 % 10U),
            (unsigned long)(sensor_values.scd_rh10 / 10U),
            (unsigned long)(sensor_values.scd_rh10 % 10U),
            (long)(sensor_values.bme_temp10 / 10),
            (unsigned long)((sensor_values.bme_temp10 < 0) ?
                (uint32_t)(-(int64_t)sensor_values.bme_temp10) % 10U :
                (uint32_t)sensor_values.bme_temp10 % 10U),
            (unsigned long)(sensor_values.bme_rh10 / 10U),
            (unsigned long)(sensor_values.bme_rh10 % 10U),
            (unsigned long)(sensor_values.bme_pressure_hpa10 / 10U),
            (unsigned long)(sensor_values.bme_pressure_hpa10 % 10U),
            (long)((sensor_values.bme_temp10 -
                    sensor_values.room_temp10) / 10),
            (unsigned long)(((sensor_values.bme_temp10 -
                              sensor_values.room_temp10) < 0) ?
                (uint32_t)(-(int64_t)(sensor_values.bme_temp10 -
                                      sensor_values.room_temp10)) % 10U :
                (uint32_t)(sensor_values.bme_temp10 -
                           sensor_values.room_temp10) % 10U),
            (unsigned long)sensor_values.bme_gas_ohms,
            (unsigned long)sensor_values.bme_gas_ratio_percent,
            App_OnOff(sensor_values.bme_gas_alarm),
            App_OnOff(sensor_values.bme_gas_valid),
            App_OnOff(sensor_values.bme_heater_stable),
            sensor_values.flame_detected ? "DETECTED" : "CLEAR",
            (unsigned int)sensor_values.flame_adc_raw,
            (unsigned long)sensor_values.flame_voltage_mv);
    }
    else if (App_CommandEquals(command, "NETWORK"))
    {
        MCP2221A_Console_Printf(
            "\r\nESP-01=%s WIFI=%s BLYNK=%s\r\n"
            "Location=%s city=%s timezone=%s\r\n"
            "Weather=%s outside=%d C condition=%s\r\n",
            App_OnOff(app_runtime.esp_ok),
            App_OnOff(app_runtime.wifi_ok),
            App_OnOff(app_runtime.blynk_ok),
            App_OnOff(internet_weather.location_valid),
            internet_weather.city,
            internet_weather.timezone,
            App_OnOff(internet_weather.weather_valid),
            (int)internet_weather.temperature_c,
            internet_weather.condition);
    }
    else if (App_CommandEquals(command, "MEMORY"))
    {
        MCP2221A_Console_Printf(
            "\r\nSDRAM=%s base=0x%08lX size=8 MiB FMC clock=66.7 MHz\r\n"
            "QSPI=%s JEDEC expected=EF4018 assets=%s\r\n",
            app_runtime.sdram_ok ? "PASS" : "FAIL",
            (unsigned long)SDRAM_BASE_ADDRESS,
            app_runtime.qspi_ok ? "PASS" : "FAIL",
            app_runtime.qspi_ok ? "READY" : "UNAVAILABLE");
    }
    else
    {
        MCP2221A_Console_Print("Unknown command. Type HELP.\r\n");
    }
}

static SystemAlarm SystemAlarm_Evaluate(bool measurements_ready,
                                        bool scd_healthy,
                                        bool bme_healthy,
                                        bool flame_sensor_healthy,
                                        const SensorValues *values)
{
    SystemAlarm result = {ALARM_NONE, BUZZER_OFF};

    if (!measurements_ready || (values == NULL))
        return result;

    /* Highest priority: immediate fire or large VOC/gas response. */
    if (values->flame_detected)
    {
        result.type = ALARM_FLAME;
        result.buzzer = BUZZER_CRITICAL;
        return result;
    }

    if (bme_healthy && values->bme_gas_alarm)
    {
        result.type = ALARM_AIR_CHANGE;
        result.buzzer = BUZZER_CRITICAL;
        return result;
    }

    if (!scd_healthy)
    {
        result.type = ALARM_SCD41_FAULT;
        result.buzzer = BUZZER_FAULT;
        return result;
    }

    if (!bme_healthy)
    {
        result.type = ALARM_BME688_FAULT;
        result.buzzer = BUZZER_FAULT;
        return result;
    }

    if (!flame_sensor_healthy)
    {
        result.type = ALARM_FLAME_SENSOR_FAULT;
        result.buzzer = BUZZER_FAULT;
        return result;
    }

    if (values->scd_co2 >= CO2_DANGER_PPM)
    {
        result.type = ALARM_CO2_DANGER;
        result.buzzer = BUZZER_CRITICAL;
        return result;
    }

    if (values->scd_co2 >= CO2_POOR_PPM)
    {
        result.type = ALARM_CO2_HIGH;
        result.buzzer = BUZZER_WARNING;
        return result;
    }

    if (values->room_temp10 <= ROOM_TEMP_LOW10)
    {
        result.type = ALARM_TEMPERATURE_LOW;
        result.buzzer = BUZZER_WARNING;
        return result;
    }

    if (values->room_temp10 >= ROOM_TEMP_HIGH10)
    {
        result.type = ALARM_TEMPERATURE_HIGH;
        result.buzzer = BUZZER_WARNING;
        return result;
    }

    if (values->room_rh10 <= ROOM_RH_LOW10)
    {
        result.type = ALARM_HUMIDITY_LOW;
        result.buzzer = BUZZER_WARNING;
        return result;
    }

    if (values->room_rh10 >= ROOM_RH_HIGH10)
    {
        result.type = ALARM_HUMIDITY_HIGH;
        result.buzzer = BUZZER_WARNING;
        return result;
    }

    if ((values->bme_pressure_hpa10 < PRESSURE_MIN_HPA10) ||
        (values->bme_pressure_hpa10 > PRESSURE_MAX_HPA10))
    {
        result.type = ALARM_PRESSURE_INVALID;
        result.buzzer = BUZZER_FAULT;
    }

    return result;
}

static void Buzzer_Update(BuzzerPattern pattern, uint32_t now)
{
    bool buzzer_on = false;

    switch (pattern)
    {
        case BUZZER_CRITICAL:
            /* Fast repeating alarm: 250 ms on, 150 ms off. */
            buzzer_on = ((now % 400U) < 250U);
            break;

        case BUZZER_WARNING:
            /* Comfort/CO2 warning: one reminder every 30 seconds. */
            buzzer_on = ((now % 30000U) < 160U);
            break;

        case BUZZER_FAULT:
            /* Maintenance fault: one quiet reminder every minute. */
            buzzer_on = ((now % 60000U) < 100U);
            break;

        default:
            buzzer_on = false;
            break;
    }

    HAL_GPIO_WritePin(BUZZER_GPIO_Port,
                      BUZZER_Pin,
                      buzzer_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static APP_UNUSED void GUI_ShowDashboard(bool scd_ok,
                              bool bme_ok,
                              bool wifi_ok,
                              bool blynk_ok,
                              const SensorValues *values)
{
    char line[40];
    bool force_update;
    bool temperature_changed;
    bool humidity_changed;
    bool pressure_changed;
    bool co2_changed;
    bool air_changed;
    bool footer_changed;
    AirQualityLevel air_quality;
    uint16_t air_colour;
    const char *room_state;

    if (values == NULL)
        return;

    /* Returning from an alert requires one complete dashboard rebuild. */
    gui_alarm_drawn = false;
    gui_last_alarm = ALARM_NONE;

    force_update =
        (!gui_dashboard_drawn) || (!gui_dashboard_cache_valid);

    temperature_changed =
        force_update ||
        (gui_dashboard_cache.room_temp10 != values->room_temp10);

    humidity_changed =
        force_update ||
        (gui_dashboard_cache.room_rh10 != values->room_rh10);

    pressure_changed =
        force_update ||
        (gui_dashboard_cache.bme_pressure_hpa10 !=
         values->bme_pressure_hpa10);

    co2_changed =
        force_update ||
        (gui_dashboard_cache.scd_co2 != values->scd_co2);

    air_changed =
        force_update || co2_changed ||
        (gui_dashboard_cache.bme_gas_baseline_ready !=
         values->bme_gas_baseline_ready) ||
        (gui_dashboard_cache.bme_heater_stable !=
         values->bme_heater_stable) ||
        (gui_last_scd_ok != scd_ok) ||
        (gui_last_bme_ok != bme_ok);

    footer_changed =
        force_update ||
        (gui_last_wifi_ok != wifi_ok) ||
        (gui_last_blynk_ok != blynk_ok) ||
        (gui_last_scd_ok != scd_ok) ||
        (gui_last_bme_ok != bme_ok);

    if (!(temperature_changed || humidity_changed || pressure_changed ||
          co2_changed || air_changed || footer_changed))
    {
        return;
    }

    air_quality = AirQuality_Get(scd_ok, bme_ok, values);
    air_colour = AirQuality_Colour(air_quality);
    room_state =
        (air_quality == AIR_QUALITY_GOOD) ? "ALL GOOD" : "CHECK AIR";

    if (!gui_dashboard_drawn)
    {
        /* Restore the complete QSPI background only when first entering or
         * returning to the dashboard. Later value updates remain local. */
        if (!LCD_LoadBackground())
            Error_Handler();

        LCD_DrawString(18U, 14U, "ROOM ONE", LCD_WHITE, 3U);
        LCD_DrawString(20U, 38U, "LIVE ENVIRONMENT", LCD_MUTED, 1U);

        GUI_DrawCard(14U, 52U, 142U, 72U, LCD_CARD, LCD_CYAN);
        LCD_DrawIcon(29U, 58U, QSPI_ICON_TEMPERATURE, LCD_CYAN);
        LCD_DrawString(54U, 61U, "TEMPERATURE", LCD_MUTED, 1U);

        GUI_DrawCard(169U, 52U, 142U, 72U, LCD_CARD, LCD_TEAL);
        LCD_DrawIcon(184U, 58U, QSPI_ICON_HUMIDITY, LCD_TEAL);
        LCD_DrawString(209U, 61U, "HUMIDITY", LCD_MUTED, 1U);

        GUI_DrawCard(324U, 52U, 142U, 72U, LCD_CARD, LCD_BLUE);
        LCD_DrawIcon(339U, 58U, QSPI_ICON_PRESSURE, LCD_BLUE);
        LCD_DrawString(364U, 61U, "PRESSURE", LCD_MUTED, 1U);

        GUI_DrawCard(14U, 136U, 190U, 91U,
                     LCD_CARD_ALT, LCD_CYAN);
        LCD_DrawIcon(29U, 142U, QSPI_ICON_CO2, LCD_CYAN);
        LCD_DrawString(54U, 146U, "CARBON DIOXIDE", LCD_MUTED, 1U);

        GUI_DrawCard(217U, 136U, 249U, 91U,
                     LCD_CARD_ALT, air_colour);
        LCD_DrawIcon(232U, 142U, QSPI_ICON_AIR_QUALITY, air_colour);
        LCD_DrawString(257U, 146U, "AIR QUALITY", LCD_MUTED, 1U);

        gui_dashboard_drawn = true;
    }

    if (air_changed)
    {
        /* Clear and redraw only the status badge and air-quality values. */
        LCD_RestoreBackgroundRectangle(363U, 14U, 100U, 28U);
        LCD_FillRectangle(365U, 16U, 96U, 24U, LCD_CARD_ALT);
        GUI_DrawRectangle(365U, 16U, 96U, 24U, air_colour);
        LCD_DrawIcon(369U, 18U, QSPI_ICON_AIR_QUALITY, air_colour);
        LCD_DrawString(393U, 20U, room_state, air_colour, 1U);

        LCD_FillRectangle(221U, 163U, 241U, 59U, LCD_CARD_ALT);
        GUI_DrawRectangle(217U, 136U, 249U, 91U, air_colour);
        LCD_FillRectangle(225U, 144U, 3U, 12U, air_colour);
        LCD_DrawIcon(232U, 142U, QSPI_ICON_AIR_QUALITY, air_colour);
        GUI_DrawCenteredInBox(217U, 249U, 169U,
                              AirQuality_Name(air_quality),
                              air_colour,
                              3U);
        GUI_DrawCenteredInBox(
            217U,
            249U,
            204U,
            AirQuality_Explanation(air_quality, values),
            LCD_WHITE,
            1U
        );
    }

    if (temperature_changed)
    {
        LCD_FillRectangle(18U, 80U, 134U, 39U, LCD_CARD);
        GUI_FormatSignedTenths(line,
                               sizeof(line),
                               values->room_temp10,
                               "C");
        GUI_DrawCenteredInBox(14U, 142U, 86U,
                              line, LCD_WHITE, 3U);
    }

    if (humidity_changed)
    {
        LCD_FillRectangle(173U, 80U, 134U, 39U, LCD_CARD);
        snprintf(line,
                 sizeof(line),
                 "%lu.%lu %%",
                 (unsigned long)(values->room_rh10 / 10U),
                 (unsigned long)(values->room_rh10 % 10U));
        GUI_DrawCenteredInBox(169U, 142U, 86U,
                              line, LCD_WHITE, 3U);
    }

    if (pressure_changed)
    {
        LCD_FillRectangle(328U, 78U, 134U, 41U, LCD_CARD);
        snprintf(line,
                 sizeof(line),
                 "%lu.%lu",
                 (unsigned long)(values->bme_pressure_hpa10 / 10U),
                 (unsigned long)(values->bme_pressure_hpa10 % 10U));
        GUI_DrawCenteredInBox(324U, 142U, 82U,
                              line, LCD_WHITE, 2U);
        GUI_DrawCenteredInBox(324U, 142U, 104U,
                              "HPA", LCD_MUTED, 1U);
    }

    if (co2_changed)
    {
        LCD_FillRectangle(18U, 166U, 182U, 56U, LCD_CARD_ALT);
        snprintf(line,
                 sizeof(line),
                 "%u PPM",
                 (unsigned int)values->scd_co2);
        GUI_DrawCenteredInBox(14U, 190U, 172U,
                              line, LCD_WHITE, 3U);
        GUI_DrawCenteredInBox(
            14U,
            190U,
            207U,
            (values->scd_co2 < CO2_FAIR_PPM) ?
                "NORMAL" : "VENTILATE",
            (values->scd_co2 < CO2_FAIR_PPM) ?
                LCD_GREEN : LCD_YELLOW,
            1U
        );
    }

    if (footer_changed)
    {
        LCD_RestoreBackgroundRectangle(0U, 244U, LCD_WIDTH, 24U);
        LCD_DrawIcon(8U, 246U, QSPI_ICON_WIFI,
                     wifi_ok ? LCD_CYAN : LCD_MUTED);
        LCD_DrawString(32U, 247U,
                       wifi_ok ? "WIFI ON" : "WIFI OFF",
                       wifi_ok ? LCD_CYAN : LCD_MUTED,
                       1U);

        LCD_DrawIcon(142U, 246U, QSPI_ICON_CLOUD,
                     blynk_ok ? LCD_GREEN : LCD_MUTED);
        LCD_DrawString(166U, 247U,
                       blynk_ok ? "CLOUD ON" : "CLOUD OFF",
                       blynk_ok ? LCD_GREEN : LCD_MUTED,
                       1U);

        LCD_DrawString(330U, 247U,
                       (scd_ok && bme_ok) ?
                           "SENSORS OK" : "SENSOR CHECK",
                       (scd_ok && bme_ok) ? LCD_CYAN : LCD_YELLOW,
                       1U);
    }

    gui_dashboard_cache = *values;
    gui_last_scd_ok = scd_ok;
    gui_last_bme_ok = bme_ok;
    gui_last_wifi_ok = wifi_ok;
    gui_last_blynk_ok = blynk_ok;
    gui_dashboard_cache_valid = true;

    LCD_PresentTextLayer();
}

static void GUI_ShowAlarm(SystemAlarm alarm,
                          bool flash,
                          const SensorValues *values)
{
    const char *title = "SYSTEM ALERT";
    const char *detail = "CHECK SYSTEM";
    const char *instruction = "INSPECT THE HUB";
    uint16_t background = flash ? LCD_DANGER_BG : LCD_BLACK;
    uint16_t accent = LCD_RED;
    char value_line[40] = "";
    bool rebuild_alarm;
    bool value_changed;

    if (values == NULL)
        return;

    switch (alarm.type)
    {
        case ALARM_FLAME:
            title = "FLAME DETECTED";
            detail = "FIRE SENSOR ACTIVE";
            instruction = "LEAVE ROOM AND CHECK FIRE";
            break;

        case ALARM_AIR_CHANGE:
            title = "AIR QUALITY ALERT";
            detail = "POSSIBLE GAS OR VOC CHANGE";
            instruction = "VENTILATE AND CHECK THE ROOM";
            break;

        case ALARM_CO2_DANGER:
            title = "CO2 DANGER";
            detail = "VERY HIGH CARBON DIOXIDE";
            instruction = "LEAVE ROOM AND VENTILATE";
            snprintf(value_line,
                     sizeof(value_line),
                     "%u PPM",
                     (unsigned int)values->scd_co2);
            break;

        case ALARM_CO2_HIGH:
            title = "CO2 TOO HIGH";
            detail = "FRESH AIR IS REQUIRED";
            instruction = "OPEN WINDOWS NOW";
            accent = LCD_ORANGE;
            background = flash ? LCD_WARNING_BG : LCD_BLACK;
            snprintf(value_line,
                     sizeof(value_line),
                     "%u PPM",
                     (unsigned int)values->scd_co2);
            break;

        case ALARM_TEMPERATURE_HIGH:
            title = "ROOM TOO HOT";
            detail = "TEMPERATURE ABOVE LIMIT";
            instruction = "CHECK HEATING AND VENTILATE";
            accent = LCD_ORANGE;
            background = flash ? LCD_WARNING_BG : LCD_BLACK;
            GUI_FormatSignedTenths(value_line,
                                   sizeof(value_line),
                                   values->room_temp10,
                                   "C");
            break;

        case ALARM_TEMPERATURE_LOW:
            title = "ROOM TOO COLD";
            detail = "TEMPERATURE BELOW LIMIT";
            instruction = "CHECK HEATING";
            accent = LCD_CYAN;
            background = LCD_DARK_BLUE;
            GUI_FormatSignedTenths(value_line,
                                   sizeof(value_line),
                                   values->room_temp10,
                                   "C");
            break;

        case ALARM_HUMIDITY_HIGH:
            title = "HUMIDITY TOO HIGH";
            detail = "CONDENSATION OR MOLD RISK";
            instruction = "VENTILATE THE ROOM";
            accent = LCD_ORANGE;
            background = flash ? LCD_WARNING_BG : LCD_BLACK;
            snprintf(value_line,
                     sizeof(value_line),
                     "%lu.%lu %%",
                     (unsigned long)(values->room_rh10 / 10U),
                     (unsigned long)(values->room_rh10 % 10U));
            break;

        case ALARM_HUMIDITY_LOW:
            title = "AIR TOO DRY";
            detail = "HUMIDITY BELOW LIMIT";
            instruction = "CONSIDER A HUMIDIFIER";
            accent = LCD_YELLOW;
            background = LCD_DARK_BLUE;
            snprintf(value_line,
                     sizeof(value_line),
                     "%lu.%lu %%",
                     (unsigned long)(values->room_rh10 / 10U),
                     (unsigned long)(values->room_rh10 % 10U));
            break;

        case ALARM_SCD41_FAULT:
            title = "CO2 SENSOR FAULT";
            detail = "SCD41 IS NOT RESPONDING";
            instruction = "CHECK I2C4 AND POWER";
            accent = LCD_YELLOW;
            background = LCD_DARK_BLUE;
            break;

        case ALARM_BME688_FAULT:
            title = "AIR SENSOR FAULT";
            detail = "BME688 IS NOT RESPONDING";
            instruction = "CHECK I2C3 AND POWER";
            accent = LCD_YELLOW;
            background = LCD_DARK_BLUE;
            break;

        case ALARM_FLAME_SENSOR_FAULT:
            title = "FLAME SENSOR FAULT";
            detail = "ANALOG READING FAILED";
            instruction = "CHECK PA1 AND SENSOR POWER";
            accent = LCD_YELLOW;
            background = LCD_DARK_BLUE;
            break;

        case ALARM_PRESSURE_INVALID:
            title = "PRESSURE ERROR";
            detail = "READING IS NOT PLAUSIBLE";
            instruction = "CHECK THE BME688";
            accent = LCD_YELLOW;
            background = LCD_DARK_BLUE;
            break;

        default:
            break;
    }

    rebuild_alarm =
        (!gui_alarm_drawn) ||
        (gui_last_alarm != alarm.type) ||
        (gui_last_alarm_background != flash);

    value_changed =
        (strcmp(gui_last_alarm_value_line, value_line) != 0);

    if (!rebuild_alarm && !value_changed)
        return;

    /* Force a complete dashboard rebuild when the alarm later clears. */
    gui_dashboard_drawn = false;
    gui_dashboard_cache_valid = false;

    if (rebuild_alarm)
    {
        LCD_ClearTextLayer(background);
        GUI_DrawRectangle(8U, 8U, 464U, 256U, accent);
        GUI_DrawRectangle(13U, 13U, 454U, 246U, accent);

        GUI_DrawCenteredInBox(0U, LCD_WIDTH, 35U,
                              "! ALERT !", accent, 4U);
        GUI_DrawCenteredInBox(0U, LCD_WIDTH, 92U,
                              title, LCD_WHITE, 3U);
        GUI_DrawCenteredInBox(0U, LCD_WIDTH, 178U,
                              detail, LCD_WHITE, 2U);
        GUI_DrawCenteredInBox(0U, LCD_WIDTH, 220U,
                              instruction, LCD_WHITE, 1U);
    }
    else
    {
        /* Same alarm, new numeric reading: repaint only its value area. */
        LCD_FillRectangle(20U, 125U, 440U, 42U, background);
    }

    if (value_line[0] != '\0')
    {
        GUI_DrawCenteredInBox(0U, LCD_WIDTH, 132U,
                              value_line, accent, 3U);
    }

    gui_alarm_drawn = true;
    gui_last_alarm = alarm.type;
    gui_last_alarm_background = flash;
    snprintf(gui_last_alarm_value_line,
             sizeof(gui_last_alarm_value_line),
             "%s",
             value_line);

    LCD_PresentTextLayer();
}

/* =========================================================
 * SDRAM initialization sequence
 * ========================================================= */

static HAL_StatusTypeDef SDRAM_Startup(void)
{
    FMC_SDRAM_CommandTypeDef cmd;
    uint32_t mode_register;

    memset(&cmd, 0, sizeof(cmd));
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber = 1;
    cmd.ModeRegisterDefinition = 0;

    /* Enable SDRAM clock */
    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;

    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 1000) != HAL_OK)
        return HAL_ERROR;

    /* Datasheet requires at least 100 us */
    HAL_Delay(1);

    /* Precharge all banks */
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;

    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 1000) != HAL_OK)
        return HAL_ERROR;

    /* Eight auto-refresh cycles */
    cmd.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8;

    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 1000) != HAL_OK)
        return HAL_ERROR;

    mode_register =
        SDRAM_MODEREG_BURST_LENGTH_1 |
        SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
        SDRAM_MODEREG_CAS_LATENCY_3 |
        SDRAM_MODEREG_OPERATING_MODE_STANDARD |
        SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    /* Load mode register */
    cmd.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber = 1;
    cmd.ModeRegisterDefinition = mode_register;

    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, 1000) != HAL_OK)
        return HAL_ERROR;

    /* 64 ms / 4096 rows at 66.667 MHz, minus controller margin. */
    return HAL_SDRAM_ProgramRefreshRate(
        &hsdram1,
        SDRAM_REFRESH_COUNT
    );
}

/*
 * Destructive memory-test code is retained only as historical reference.
 * It is excluded from the normal application so it cannot overwrite the
 * live SDRAM frames or erase a QSPI sector. Use SDRAM_Diagnostic_main.c when
 * a dedicated bench test is intentionally required.
 */
#if 0
/* Destructive full 8 MB test */
static APP_UNUSED bool SDRAM_Test(void)
{
    volatile uint32_t *memory =
        (volatile uint32_t *)SDRAM_BASE_ADDRESS;

    bool cache_was_enabled =
        (SCB->CCR & SCB_CCR_DC_Msk) != 0U;

    sdram_bad_address = 0;
    sdram_expected = 0;
    sdram_actual = 0;

    if (cache_was_enabled)
    {
        SCB_CleanDCache();
        SCB_DisableDCache();
    }

    for (uint32_t pass = 0; pass < 2U; pass++)
    {
        /* Write test pattern */
        for (uint32_t i = 0; i < SDRAM_WORD_COUNT; i++)
        {
            uint32_t pattern =
                (i * 0x9E3779B9UL) ^ 0xA5A55A5AUL;

            if (pass != 0U)
                pattern = ~pattern;

            memory[i] = pattern;
        }

        __DSB();

        /* Read and verify */
        for (uint32_t i = 0; i < SDRAM_WORD_COUNT; i++)
        {
            uint32_t expected =
                (i * 0x9E3779B9UL) ^ 0xA5A55A5AUL;

            if (pass != 0U)
                expected = ~expected;

            uint32_t actual = memory[i];

            if (actual != expected)
            {
                sdram_bad_address =
                    SDRAM_BASE_ADDRESS + (i * 4UL);
                sdram_expected = expected;
                sdram_actual = actual;

                if (cache_was_enabled)
                {
                    SCB_InvalidateDCache();
                    SCB_EnableDCache();
                }

                return false;
            }
        }
    }

    if (cache_was_enabled)
    {
        SCB_InvalidateDCache();
        SCB_EnableDCache();
    }

    return true;
}

/* =========================================================
 * QSPI helpers
 * ========================================================= */

static void QSPI_CommandDefaults(QSPI_CommandTypeDef *cmd,
                                 uint8_t instruction)
{
    memset(cmd, 0, sizeof(*cmd));

    cmd->Instruction = instruction;
    cmd->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd->AddressMode = QSPI_ADDRESS_NONE;
    cmd->AddressSize = QSPI_ADDRESS_24_BITS;
    cmd->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd->DataMode = QSPI_DATA_NONE;
    cmd->DummyCycles = 0;
    cmd->DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

static HAL_StatusTypeDef QSPI_CommandOnly(uint8_t instruction)
{
    QSPI_CommandTypeDef cmd;

    QSPI_CommandDefaults(&cmd, instruction);

    return HAL_QSPI_Command(&hqspi, &cmd, 1000);
}

static HAL_StatusTypeDef QSPI_ReadRegister(uint8_t instruction,
                                          uint8_t *value)
{
    QSPI_CommandTypeDef cmd;

    QSPI_CommandDefaults(&cmd, instruction);
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = 1;

    if (HAL_QSPI_Command(&hqspi, &cmd, 1000) != HAL_OK)
        return HAL_ERROR;

    return HAL_QSPI_Receive(&hqspi, value, 1000);
}

static HAL_StatusTypeDef QSPI_PollStatus(uint8_t match,
                                        uint8_t mask,
                                        uint32_t timeout)
{
    QSPI_CommandTypeDef cmd;
    QSPI_AutoPollingTypeDef poll;

    QSPI_CommandDefaults(&cmd, W25Q_CMD_READ_SR1);
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = 1;

    memset(&poll, 0, sizeof(poll));
    poll.Match = match;
    poll.Mask = mask;
    poll.MatchMode = QSPI_MATCH_MODE_AND;
    poll.StatusBytesSize = 1;
    poll.Interval = 0x10;
    poll.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

    return HAL_QSPI_AutoPolling(
        &hqspi,
        &cmd,
        &poll,
        timeout
    );
}

static HAL_StatusTypeDef QSPI_WaitReady(uint32_t timeout)
{
    /* SR1 bit 0 = BUSY; wait until it becomes zero */
    return QSPI_PollStatus(0x00U, 0x01U, timeout);
}

static HAL_StatusTypeDef QSPI_WriteEnable(void)
{
    if (QSPI_CommandOnly(W25Q_CMD_WRITE_ENABLE) != HAL_OK)
        return HAL_ERROR;

    /* SR1 bit 1 = write-enable latch */
    return QSPI_PollStatus(0x02U, 0x02U, 1000);
}

static HAL_StatusTypeDef QSPI_EnableQuadMode(void)
{
    QSPI_CommandTypeDef cmd;
    uint8_t status2;

    if (QSPI_ReadRegister(W25Q_CMD_READ_SR2, &status2) != HAL_OK)
        return HAL_ERROR;

    /* QE is status-register-2 bit 1 */
    if ((status2 & 0x02U) != 0U)
        return HAL_OK;

    status2 |= 0x02U;

    if (QSPI_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    QSPI_CommandDefaults(&cmd, W25Q_CMD_WRITE_SR2);
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = 1;

    if (HAL_QSPI_Command(&hqspi, &cmd, 1000) != HAL_OK)
        return HAL_ERROR;

    if (HAL_QSPI_Transmit(&hqspi, &status2, 1000) != HAL_OK)
        return HAL_ERROR;

    if (QSPI_WaitReady(1000) != HAL_OK)
        return HAL_ERROR;

    if (QSPI_ReadRegister(W25Q_CMD_READ_SR2, &status2) != HAL_OK)
        return HAL_ERROR;

    return ((status2 & 0x02U) != 0U) ?
           HAL_OK : HAL_ERROR;
}

/* Destructive test of the last QSPI sector */
static APP_UNUSED bool QSPI_Test(void)
{
    QSPI_CommandTypeDef cmd;
    uint8_t tx[W25Q_TEST_LENGTH];
    uint8_t rx[W25Q_TEST_LENGTH];

    qspi_fail_stage = 0;
    memset((void *)qspi_jedec_id, 0, sizeof(qspi_jedec_id));

    /* Reset flash */
    if (QSPI_CommandOnly(W25Q_CMD_RESET_ENABLE) != HAL_OK)
    {
        qspi_fail_stage = 1;
        return false;
    }

    if (QSPI_CommandOnly(W25Q_CMD_RESET) != HAL_OK)
    {
        qspi_fail_stage = 2;
        return false;
    }

    HAL_Delay(1);

    /* Read JEDEC ID */
    QSPI_CommandDefaults(&cmd, W25Q_CMD_JEDEC_ID);
    cmd.DataMode = QSPI_DATA_1_LINE;
    cmd.NbData = 3;

    if (HAL_QSPI_Command(&hqspi, &cmd, 1000) != HAL_OK ||
        HAL_QSPI_Receive(&hqspi,
                         (uint8_t *)qspi_jedec_id,
                         1000) != HAL_OK)
    {
        qspi_fail_stage = 3;
        return false;
    }

    if (qspi_jedec_id[0] != 0xEFU ||
        qspi_jedec_id[1] != 0x40U ||
        qspi_jedec_id[2] != 0x18U)
    {
        qspi_fail_stage = 4;
        return false;
    }

    if (QSPI_EnableQuadMode() != HAL_OK)
    {
        qspi_fail_stage = 5;
        return false;
    }

    /* Erase last 4 KB sector */
    if (QSPI_WriteEnable() != HAL_OK)
    {
        qspi_fail_stage = 6;
        return false;
    }

    QSPI_CommandDefaults(&cmd, W25Q_CMD_SECTOR_ERASE);
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.Address = W25Q_TEST_ADDRESS;

    if (HAL_QSPI_Command(&hqspi, &cmd, 1000) != HAL_OK)
    {
        qspi_fail_stage = 7;
        return false;
    }

    if (QSPI_WaitReady(5000) != HAL_OK)
    {
        qspi_fail_stage = 8;
        return false;
    }

    /* Prepare 256-byte test pattern */
    for (uint32_t i = 0; i < W25Q_TEST_LENGTH; i++)
        tx[i] = (uint8_t)((i * 37U) ^ 0xA5U);

    memset(rx, 0, sizeof(rx));

    /* Quad page program */
    if (QSPI_WriteEnable() != HAL_OK)
    {
        qspi_fail_stage = 9;
        return false;
    }

    QSPI_CommandDefaults(&cmd, W25Q_CMD_QUAD_PAGE_PROGRAM);
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.Address = W25Q_TEST_ADDRESS;
    cmd.DataMode = QSPI_DATA_4_LINES;
    cmd.NbData = W25Q_TEST_LENGTH;

    if (HAL_QSPI_Command(&hqspi, &cmd, 1000) != HAL_OK ||
        HAL_QSPI_Transmit(&hqspi, tx, 1000) != HAL_OK)
    {
        qspi_fail_stage = 10;
        return false;
    }

    if (QSPI_WaitReady(1000) != HAL_OK)
    {
        qspi_fail_stage = 11;
        return false;
    }

    /* Quad-output fast read */
    QSPI_CommandDefaults(&cmd, W25Q_CMD_QUAD_READ);
    cmd.AddressMode = QSPI_ADDRESS_1_LINE;
    cmd.Address = W25Q_TEST_ADDRESS;
    cmd.DataMode = QSPI_DATA_4_LINES;
    cmd.DummyCycles = 8;
    cmd.NbData = W25Q_TEST_LENGTH;

    if (HAL_QSPI_Command(&hqspi, &cmd, 1000) != HAL_OK ||
        HAL_QSPI_Receive(&hqspi, rx, 1000) != HAL_OK)
    {
        qspi_fail_stage = 12;
        return false;
    }

    if (memcmp(tx, rx, W25Q_TEST_LENGTH) != 0)
    {
        qspi_fail_stage = 13;
        return false;
    }

    return true;
}/* =========================================================
 * Display result
 * ========================================================= */

static APP_UNUSED void MemoryTest_Show(bool sdram_ok, bool qspi_ok)
{
    char line[32];

    LCD_ClearTextLayer(0x0000);

    /* When SDRAM fails, show the exact mismatch directly on the LCD. */
    if (!sdram_ok)
    {
        LCD_DrawString(0, 0, "SDRAM FAIL", LCD_RED, 2);

        snprintf(line, sizeof(line),
                 "A:%08lX", (unsigned long)sdram_bad_address);
        LCD_DrawString(0, 16, line, LCD_WHITE, 2);

        snprintf(line, sizeof(line),
                 "E:%08lX", (unsigned long)sdram_expected);
        LCD_DrawString(0, 32, line, LCD_YELLOW, 2);

        snprintf(line, sizeof(line),
                 "R:%08lX", (unsigned long)sdram_actual);
        LCD_DrawString(0, 48, line, LCD_CYAN, 2);

        LCD_PresentTextLayer();
        return;
    }

    LCD_DrawString(
        0, 0,
        sdram_ok ? "SDRAM: PASS" : "SDRAM: FAIL",
        sdram_ok ? 0x07E0 : 0xF800,
        2
    );

    LCD_DrawString(
        0, 16,
        qspi_ok ? "QSPI: PASS" : "QSPI: FAIL",
        qspi_ok ? 0x07E0 : 0xF800,
        2
    );

    snprintf(line, sizeof(line),
             "ID:%02X %02X %02X",
             qspi_jedec_id[0],
             qspi_jedec_id[1],
             qspi_jedec_id[2]);

    LCD_DrawString(0, 32, line, 0xFFFF, 2);

    LCD_DrawString(
        0, 48,
        (sdram_ok && qspi_ok) ?
        "MEMORIES WORK!" : "CHECK DEBUG",
        (sdram_ok && qspi_ok) ? 0x07E0 : 0xF800,
        2
    );

    LCD_PresentTextLayer();
}

#endif

static void LCD_ShowStatus(const char *line1,
                           const char *line2,
                           uint16_t colour)
{
    /* Once the dashboard is running, background network diagnostics must
     * not erase it. UART debug output remains available on USART3. */
    if (gui_dashboard_active)
        return;

    LCD_ClearTextLayer(LCD_BLACK);
    LCD_DrawCenteredString(12U, line1, colour, 2U);
    LCD_DrawCenteredString(36U, line2, LCD_WHITE, 2U);
    LCD_PresentTextLayer();
}

/*
 * Re-apply the external-memory MPU regions from a protected USER CODE
 * section. This keeps the board bootable even if CubeMX regenerates the
 * original Region 1 size as 64 bytes instead of 64 KB.
 */
static void MPU_FixExternalMemory(void)
{
    MPU_Region_InitTypeDef region = {0};

    HAL_MPU_Disable();

    /* FMC and QUADSPI control registers. */
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = 0xA0000000UL;
    region.Size = MPU_REGION_SIZE_64KB;
    region.SubRegionDisable = 0x00;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    /*
     * External 8 MB SDRAM as normal, non-cacheable memory. This is the
     * simplest safe policy for a framebuffer shared by the CPU and LTDC:
     * every CPU pixel write is immediately visible to the display engine.
     */
    region.Number = MPU_REGION_NUMBER2;
    region.BaseAddress = SDRAM_BASE_ADDRESS;
    region.Size = MPU_REGION_SIZE_8MB;
    region.SubRegionDisable = 0x00;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    /* W25Q128 memory-mapped window: 16 MB at 0x90000000. */
    region.Number = MPU_REGION_NUMBER3;
    region.BaseAddress = QSPI_MEMORY_MAPPED_BASE;
    region.Size = MPU_REGION_SIZE_16MB;
    region.SubRegionDisable = 0x00;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    __DSB();
    __ISB();
}
static HAL_StatusTypeDef FlameSensor_ReadADC(uint16_t *raw)
{
    if (raw == NULL)
    {
        return HAL_ERROR;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 20U) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        return HAL_ERROR;
    }

    *raw = (uint16_t)HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    return HAL_OK;
}

static bool FlameSensor_ReadAll(SensorValues *values)
{
    if (values == NULL)
        return false;

    /* The LMV393 open-drain output is active-low. */
    values->flame_detected =
        (HAL_GPIO_ReadPin(FLAME_D0_GPIO_Port,
                          FLAME_D0_Pin) == GPIO_PIN_RESET);

    if (FlameSensor_ReadADC(&values->flame_adc_raw) != HAL_OK)
        return false;

    values->flame_voltage_mv =
        ((uint32_t)values->flame_adc_raw * 3300UL) / 4095UL;

    return true;
}

static void GasMonitor_Update(SensorValues *values)
{
    static uint32_t baseline_ohms = 0U;
    static uint16_t baseline_samples = 0U;
    static uint8_t trigger_samples = 0U;
    static uint8_t clear_samples = 0U;
    static bool alarm = false;
    uint32_t ratio_percent;

    if (values == NULL)
        return;

    values->bme_gas_baseline_ohms = baseline_ohms;
    values->bme_gas_ratio_percent = 0U;
    values->bme_gas_baseline_ready = false;
    values->bme_gas_alarm = false;

    if (!values->bme_gas_valid ||
        !values->bme_heater_stable ||
        (values->bme_gas_ohms == 0U))
    {
        trigger_samples = 0U;
        clear_samples = 0U;
        return;
    }

    /* Five minutes of valid heater-stable data at a 5-second sample period.
     * This prevents warm-up drift from being treated as a gas alarm. */
    if (baseline_samples < GAS_BASELINE_SAMPLES)
    {
        if (baseline_ohms == 0U)
        {
            baseline_ohms = values->bme_gas_ohms;
        }
        else
        {
            baseline_ohms =
                (uint32_t)((((uint64_t)baseline_ohms * 7ULL) +
                            values->bme_gas_ohms) / 8ULL);
        }

        baseline_samples++;
        values->bme_gas_baseline_ohms = baseline_ohms;
        values->bme_gas_ratio_percent = 100U;
        values->bme_gas_baseline_ready =
            (baseline_samples >= GAS_BASELINE_SAMPLES);
        return;
    }

    ratio_percent =
        (uint32_t)(((uint64_t)values->bme_gas_ohms * 100ULL) /
                   baseline_ohms);

    /* BME688 gas resistance normally falls when reducing VOC/gas rises.
     * Require six consecutive low readings (30 seconds). Upward resistance
     * drift is ignored by this heuristic; it is not classified as safe. */
    if (!alarm)
    {
        clear_samples = 0U;

        if (ratio_percent <= GAS_TRIGGER_RATIO_PERCENT)
        {
            if (trigger_samples < GAS_ALARM_CONFIRM_SAMPLES)
                trigger_samples++;
        }
        else
        {
            trigger_samples = 0U;
        }

        if (trigger_samples >= GAS_ALARM_CONFIRM_SAMPLES)
        {
            alarm = true;
            trigger_samples = 0U;
        }
    }
    else
    {
        trigger_samples = 0U;

        if (ratio_percent >= GAS_CLEAR_RATIO_PERCENT)
        {
            if (clear_samples < GAS_CLEAR_CONFIRM_SAMPLES)
                clear_samples++;
        }
        else
        {
            clear_samples = 0U;
        }

        if (clear_samples >= GAS_CLEAR_CONFIRM_SAMPLES)
        {
            alarm = false;
            clear_samples = 0U;
        }
    }

    /* Slowly follow long-term clean-air drift when no alarm is active. */
    if (!alarm)
    {
        baseline_ohms =
            (uint32_t)((((uint64_t)baseline_ohms * 999ULL) +
                        values->bme_gas_ohms) / 1000ULL);
    }

    values->bme_gas_baseline_ohms = baseline_ohms;
    values->bme_gas_ratio_percent = ratio_percent;
    values->bme_gas_baseline_ready = true;
    values->bme_gas_alarm = alarm;
}


static APP_UNUSED void FlameSensor_TestScreen(void)
{
    uint16_t adc_raw;
    uint32_t voltage_mv;
    GPIO_PinState digital_state;
    bool flame_detected;
    char line[40];
    char uart_message[100];

    digital_state = HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2);

    /*
     * LMV393 output is active-low:
     * LOW  = flame detected
     * HIGH = no flame
     */
    flame_detected = (digital_state == GPIO_PIN_RESET);

    LCD_ClearTextLayer(LCD_BLACK);

    LCD_DrawString(0, 0,
                   "FLAME SENSOR TEST",
                   LCD_CYAN, 2);

    if (FlameSensor_ReadADC(&adc_raw) == HAL_OK)
    {
        voltage_mv =
            ((uint32_t)adc_raw * 3300UL) / 4095UL;

        snprintf(line, sizeof(line),
                 "ADC: %u",
                 adc_raw);

        LCD_DrawString(0, 20,
                       line,
                       LCD_WHITE, 2);

        snprintf(line, sizeof(line),
                 "A0: %lu.%03lu V",
                 (unsigned long)(voltage_mv / 1000UL),
                 (unsigned long)(voltage_mv % 1000UL));

        LCD_DrawString(0, 40,
                       line,
                       LCD_YELLOW, 2);

        snprintf(uart_message,
                 sizeof(uart_message),
                 "A0=%u, Voltage=%lumV, D0=%u, Flame=%u\r\n",
                 adc_raw,
                 (unsigned long)voltage_mv,
                 digital_state == GPIO_PIN_SET ? 1U : 0U,
                 flame_detected ? 1U : 0U);

        MCP2221A_Console_Print(uart_message);
    }
    else
    {
        LCD_DrawString(0, 20,
                       "ADC ERROR",
                       LCD_RED, 2);
    }

    if (flame_detected)
    {
        LCD_DrawString(0, 65,
                       "FLAME DETECTED!",
                       LCD_RED, 3);
    }
    else
    {
        LCD_DrawString(0, 65,
                       "NO FLAME",
                       LCD_GREEN, 3);
    }

    snprintf(line, sizeof(line),
             "D0: %s",
             digital_state == GPIO_PIN_SET ?
             "HIGH" : "LOW");

    LCD_DrawString(0, 95,
                   line,
                   LCD_WHITE, 2);

    LCD_PresentTextLayer();
}
static APP_UNUSED void BME688_GasTestScreen(bool sensor_online)
{
    static uint32_t baseline_ohms = 0U;
    static uint16_t baseline_samples = 0U;
    static bool gas_change_detected = false;

    uint32_t gas_ohms;
    uint32_t gas_kohm10;
    uint32_t baseline_kohm10;
    uint32_t ratio_percent = 0U;
    char line[40];

    LCD_ClearTextLayer(LCD_BLACK);

    LCD_DrawString(0, 0,
                   "BME688 GAS TEST",
                   LCD_CYAN, 2);

    if (!sensor_online)
    {
        LCD_DrawString(0, 25,
                       "I2C3 SENSOR ERROR",
                       LCD_RED, 2);

        LCD_PresentTextLayer();
        return;
    }

    if (!BME688_Read(&sensor_values))
    {
        LCD_DrawString(0, 25,
                       "MEASUREMENT ERROR",
                       LCD_RED, 2);

        LCD_PresentTextLayer();
        return;
    }

    gas_ohms = sensor_values.bme_gas_ohms;

    /* Gas resistance displayed as kOhm with one decimal. */
    gas_kohm10 = (gas_ohms + 50U) / 100U;

    snprintf(line, sizeof(line),
             "R:%lu.%lu KOHM",
             (unsigned long)(gas_kohm10 / 10U),
             (unsigned long)(gas_kohm10 % 10U));

    LCD_DrawString(0, 20, line, LCD_WHITE, 2);

    snprintf(line, sizeof(line),
             "VALID:%s HEAT:%s",
             sensor_values.bme_gas_valid ? "YES" : "NO",
             sensor_values.bme_heater_stable ? "YES" : "NO");

    LCD_DrawString(0, 40, line,
                   (sensor_values.bme_gas_valid &&
                    sensor_values.bme_heater_stable) ?
                   LCD_GREEN : LCD_YELLOW,
                   2);

    if (sensor_values.bme_gas_valid &&
        sensor_values.bme_heater_stable)
    {
        /*
         * Learn the normal-air baseline over the first 30 valid
         * measurements.
         */
        if (baseline_samples < 30U)
        {
            if (baseline_ohms == 0U)
            {
                baseline_ohms = gas_ohms;
            }
            else
            {
                baseline_ohms =
                    (uint32_t)
                    ((((uint64_t)baseline_ohms * 7ULL) +
                       gas_ohms) / 8ULL);
            }

            baseline_samples++;

            snprintf(line, sizeof(line),
                     "BASELINE:%u/30",
                     baseline_samples);

            LCD_DrawString(0, 60, line,
                           LCD_YELLOW, 2);

            LCD_DrawString(0, 100,
                           "KEEP CLEAN AIR",
                           LCD_WHITE, 2);
        }
        else if (baseline_ohms != 0U)
        {
            ratio_percent =
                (uint32_t)(((uint64_t)gas_ohms * 100ULL) /
                           baseline_ohms);

            baseline_kohm10 =
                (baseline_ohms + 50U) / 100U;

            snprintf(line, sizeof(line),
                     "BASE:%lu.%lu KOHM",
                     (unsigned long)(baseline_kohm10 / 10U),
                     (unsigned long)(baseline_kohm10 % 10U));

            LCD_DrawString(0, 60, line,
                           LCD_WHITE, 2);

            snprintf(line, sizeof(line),
                     "RATIO:%lu PCT",
                     (unsigned long)ratio_percent);

            LCD_DrawString(0, 80, line,
                           LCD_WHITE, 2);

            /*
             * Functional test threshold only:
             * trigger if resistance changes more than 30%.
             */
            if (!gas_change_detected &&
                ((ratio_percent <
                  (100U - GAS_TRIGGER_CHANGE_PERCENT)) ||
                 (ratio_percent >
                  (100U + GAS_TRIGGER_CHANGE_PERCENT))))
            {
                gas_change_detected = true;
            }
            else if (gas_change_detected &&
                     (ratio_percent >
                      (100U - GAS_CLEAR_CHANGE_PERCENT)) &&
                     (ratio_percent <
                      (100U + GAS_CLEAR_CHANGE_PERCENT)))
            {
                gas_change_detected = false;
            }
            if (gas_change_detected)
            {
                LCD_DrawString(0, 100,
                               "GAS CHANGE!",
                               LCD_RED, 3);
            }
            else
            {
                LCD_DrawString(0, 100,
                               "AIR NORMAL",
                               LCD_GREEN, 2);

                baseline_ohms =
                    (uint32_t)
                    ((((uint64_t)baseline_ohms * 999ULL) +
                       gas_ohms) / 1000ULL);
            }
        }
    }
    else
    {
        LCD_DrawString(0, 70,
                       "HEATER WARMING",
                       LCD_YELLOW, 2);
    }

    LCD_PresentTextLayer();
}

/*
 * Network requests can wait for several seconds.  Keep sampling the
 * active-low hardware flame output during the instrumented waits to reduce
 * alarm latency. Other blocking paths still make this best-effort, not a
 * hard real-time response guarantee.
 */
static bool Safety_FlameActiveDuringNetwork(void)
{
    bool active =
        (HAL_GPIO_ReadPin(FLAME_D0_GPIO_Port,
                          FLAME_D0_Pin) == GPIO_PIN_RESET);

    /* Keep the USB debug console responsive during ESP-01 timeouts. */
    MCP2221A_Console_Process();

    if (active)
    {
        sensor_values.flame_detected = true;
        HAL_GPIO_WritePin(BUZZER_GPIO_Port,
                          BUZZER_Pin,
                          GPIO_PIN_SET);
    }

    return active;
}

static uint16_t ESP01_SendCommand(const char *command,
                                  char *response,
                                  uint16_t capacity,
                                  uint32_t timeout_ms)
{
    uint8_t byte;
    uint16_t used = 0U;
    uint32_t start;

    if ((response == NULL) || (capacity < 2U))
        return 0U;

    response[0] = '\0';

    /* Remove old bytes waiting in USART1. */
    while (HAL_UART_Receive(&huart1,
                            &byte,
                            1U,
                            2U) == HAL_OK)
    {
    }

    if (HAL_UART_Transmit(&huart1,
                          (uint8_t *)command,
                          strlen(command),
                          500U) != HAL_OK)
    {
        return 0U;
    }

    start = HAL_GetTick();

    while (((HAL_GetTick() - start) < timeout_ms) &&
           (used < (capacity - 1U)))
    {
        if (Safety_FlameActiveDuringNetwork())
            break;

        if (HAL_UART_Receive(&huart1,
                             &byte,
                             1U,
                             20U) == HAL_OK)
        {
            response[used++] = (char)byte;
            response[used] = '\0';

            if ((strstr(response, "OK") != NULL) ||
                (strstr(response, "ERROR") != NULL) ||
                (strstr(response, "FAIL") != NULL))
            {
                break;
            }
        }
    }

    return used;
}
static bool ESP01_TestScreen(void)
{
    static const uint32_t baud_rates[] =
    {
        115200U,
        9600U,
        57600U,
        38400U
    };

    char response[256];
    char line[40];
    uint32_t detected_baud = 0U;
    uint16_t received = 0U;
    bool passed = false;

    /* Give ESP-01 time to boot. */
    HAL_Delay(2500U);

    for (uint32_t i = 0U;
         i < (sizeof(baud_rates) / sizeof(baud_rates[0]));
         i++)
    {
        huart1.Init.BaudRate = baud_rates[i];

        if (HAL_UART_Init(&huart1) != HAL_OK)
            continue;

        received = ESP01_SendCommand("AT\r\n",
                                     response,
                                     sizeof(response),
                                     1200U);

        if ((received > 0U) &&
            (strstr(response, "OK") != NULL))
        {
            passed = true;
            detected_baud = baud_rates[i];
            break;
        }
    }

    LCD_ClearTextLayer(LCD_BLACK);
    LCD_DrawString(0, 0, "ESP-01 USART1", LCD_CYAN, 2);

    if (passed)
    {
        LCD_DrawString(0, 20, "AT RESPONSE: OK", LCD_GREEN, 2);

        snprintf(line, sizeof(line),
                 "BAUD: %lu",
                 (unsigned long)detected_baud);

        LCD_DrawString(0, 40, line, LCD_WHITE, 2);

        /* Optional: show complete response through MCP2221A/USART3. */
        MCP2221A_Console_Print("\r\nESP-01 RESPONSE:\r\n");
        MCP2221A_Console_Write(response, received);
    }
    else
    {
        LCD_DrawString(0, 20, "NO AT RESPONSE", LCD_RED, 2);
        LCD_DrawString(0, 40, "CHECK UART WIRES", LCD_YELLOW, 2);
    }

    LCD_PresentTextLayer();

    return passed;
}
static bool ESP01_ConnectWiFi(void)
{
    static char response[512];
    static char command[160];

    char ip_address[20] = "NO IP";
    const char *ip_start;
    const char *ip_end;
    size_t ip_length;
    uint16_t received;

    /* Disable command echo. Failure here is not critical. */
    ESP01_SendCommand("ATE0\r\n",
                      response,
                      sizeof(response),
                      1000U);

    LCD_ShowStatus("ESP-01 WIFI",
                   "STATION MODE",
                   LCD_CYAN);

    received = ESP01_SendCommand("AT+CWMODE=1\r\n",
                                 response,
                                 sizeof(response),
                                 2000U);

    if ((received == 0U) ||
        (strstr(response, "OK") == NULL))
    {
        LCD_ShowStatus("ESP-01 WIFI",
                       "MODE FAILED",
                       LCD_RED);

        return false;
    }

    snprintf(command,
             sizeof(command),
             "AT+CWJAP=\"%s\",\"%s\"\r\n",
             WIFI_SSID,
             WIFI_PASSWORD);

    LCD_ShowStatus("ESP-01 WIFI",
                   "CONNECTING...",
                   LCD_YELLOW);

    /*
     * Joining Wi-Fi can take several seconds.
     */
    received = ESP01_SendCommand(command,
                                 response,
                                 sizeof(response),
                                 30000U);

    /* Send the complete ESP response to USART3 for debugging. */
    MCP2221A_Console_Print("\r\nCWJAP RESPONSE:\r\n");
    MCP2221A_Console_Write(response, received);

    if ((strstr(response, "OK") == NULL) &&
        (strstr(response, "WIFI GOT IP") == NULL))
    {
        LCD_ClearTextLayer(LCD_BLACK);

        LCD_DrawString(0, 0,
                       "ESP-01 WIFI",
                       LCD_CYAN, 2);

        LCD_DrawString(0, 20,
                       "CONNECTION FAILED",
                       LCD_RED, 2);

        if (strstr(response, "FAIL") != NULL)
        {
            LCD_DrawString(0, 40,
                           "CHECK PASSWORD",
                           LCD_YELLOW, 2);
        }
        else
        {
            LCD_DrawString(0, 40,
                           "CHECK 2.4GHZ WIFI",
                           LCD_YELLOW, 2);
        }

        LCD_PresentTextLayer();
        return false;
    }

    /*
     * Enable automatic reconnection after future power cycles.
     * Ignore the response because some older AT firmware versions
     * may not support this command.
     */
    ESP01_SendCommand("AT+CWAUTOCONN=1\r\n",
                      response,
                      sizeof(response),
                      1000U);

    /* Request the IP address. */
    received = ESP01_SendCommand("AT+CIFSR\r\n",
                                 response,
                                 sizeof(response),
                                 3000U);

    /*
     * Expected response:
     * +CIFSR:STAIP,"192.168.1.123"
     */
    ip_start = strstr(response, "+CIFSR:STAIP,\"");

    if (ip_start != NULL)
    {
        ip_start += strlen("+CIFSR:STAIP,\"");
        ip_end = strchr(ip_start, '"');

        if (ip_end != NULL)
        {
            ip_length = (size_t)(ip_end - ip_start);

            if (ip_length >= sizeof(ip_address))
                ip_length = sizeof(ip_address) - 1U;

            memcpy(ip_address, ip_start, ip_length);
            ip_address[ip_length] = '\0';
        }
    }

    LCD_ClearTextLayer(LCD_BLACK);

    LCD_DrawString(0, 0,
                   "ESP-01 WIFI",
                   LCD_CYAN, 2);

    LCD_DrawString(0, 20,
                   "CONNECTED",
                   LCD_GREEN, 2);

    LCD_DrawString(0, 40,
                   ip_address,
                   LCD_WHITE, 2);

    LCD_PresentTextLayer();

    return true;
}
static uint16_t ESP01_ReadUntil(char *response,
                                uint16_t capacity,
                                const char *success,
                                const char *failure,
                                uint32_t timeout_ms)
{
    uint8_t byte;
    uint16_t used = 0U;
    uint32_t start = HAL_GetTick();

    if ((response == NULL) || (capacity < 2U))
        return 0U;

    response[0] = '\0';

    while (((HAL_GetTick() - start) < timeout_ms) &&
           (used < (capacity - 1U)))
    {
        if (Safety_FlameActiveDuringNetwork())
            break;

        if (HAL_UART_Receive(&huart1,
                             &byte,
                             1U,
                             20U) == HAL_OK)
        {
            response[used++] = (char)byte;
            response[used] = '\0';

            if ((success != NULL) &&
                (strstr(response, success) != NULL))
            {
                break;
            }

            if ((failure != NULL) &&
                (strstr(response, failure) != NULL))
            {
                break;
            }
        }
    }

    return used;
}

static bool ESP01_PrepareDataSend(uint16_t data_length)
{
    char command[40];
    char response[192];
    uint8_t byte;
    uint16_t received;

    /* Remove old UART data. */
    while (HAL_UART_Receive(&huart1,
                            &byte,
                            1U,
                            2U) == HAL_OK)
    {
    }

    snprintf(command,
             sizeof(command),
             "AT+CIPSEND=%u\r\n",
             (unsigned int)data_length);

    if (HAL_UART_Transmit(&huart1,
                          (uint8_t *)command,
                          strlen(command),
                          500U) != HAL_OK)
    {
        return false;
    }

    /*
     * Expected response:
     *
     * OK
     * >
     */
    received = ESP01_ReadUntil(response,
                               sizeof(response),
                               ">",
                               "ERROR",
                               5000U);

    if (strchr(response, '>') == NULL)
    {
        MCP2221A_Console_Print("\r\nCIPSEND RESPONSE:\r\n");
        MCP2221A_Console_Write(response, received);

        return false;
    }

    return true;
}

/* =========================================================
 * Plain-HTTP location, clock and outside weather
 * ========================================================= */

static bool Text_ContainsInsensitive(const char *text,
                                     const char *word)
{
    if ((text == NULL) || (word == NULL) || (*word == '\0'))
        return false;

    for (; *text != '\0'; text++)
    {
        const char *left = text;
        const char *right = word;

        while ((*left != '\0') && (*right != '\0') &&
               (tolower((unsigned char)*left) ==
                tolower((unsigned char)*right)))
        {
            left++;
            right++;
        }

        if (*right == '\0')
            return true;
    }

    return false;
}

static void Text_ToDisplayUpper(char *destination,
                                size_t capacity,
                                const char *source)
{
    size_t used = 0U;
    bool previous_space = true;

    if ((destination == NULL) || (capacity == 0U))
        return;

    if (source == NULL)
    {
        destination[0] = '\0';
        return;
    }

    while ((*source != '\0') && (used + 1U < capacity))
    {
        unsigned char character = (unsigned char)*source++;

        if ((character >= 'a') && (character <= 'z'))
            character = (unsigned char)toupper(character);

        if (((character >= 'A') && (character <= 'Z')) ||
            ((character >= '0') && (character <= '9')) ||
            (character == '-') || (character == '.') ||
            (character == '/'))
        {
            destination[used++] = (char)character;
            previous_space = false;
        }
        else if (!previous_space)
        {
            destination[used++] = ' ';
            previous_space = true;
        }
    }

    while ((used != 0U) && (destination[used - 1U] == ' '))
        used--;

    destination[used] = '\0';
}

static bool ESP01_HTTPGet(const char *host,
                          const char *path,
                          char *response,
                          uint16_t response_capacity,
                          uint32_t timeout_ms)
{
    char command[160];
    char request[640];
    char scratch[512];
    int request_length;
    uint16_t received;
    bool connected = false;

    if ((host == NULL) || (path == NULL) ||
        (response == NULL) || (response_capacity < 64U))
    {
        return false;
    }

    (void)ESP01_SendCommand("AT+CIPCLOSE\r\n",
                            scratch,
                            sizeof(scratch),
                            1200U);
    HAL_Delay(100U);

    received = ESP01_SendCommand("AT+CIPMUX=0\r\n",
                                 scratch,
                                 sizeof(scratch),
                                 2000U);
    if ((received == 0U) || (strstr(scratch, "OK") == NULL))
        return false;

    received = ESP01_SendCommand("AT+CIPMODE=0\r\n",
                                 scratch,
                                 sizeof(scratch),
                                 2000U);
    if ((received == 0U) || (strstr(scratch, "OK") == NULL))
        return false;

    snprintf(command,
             sizeof(command),
             "AT+CIPSTART=\"TCP\",\"%s\",80\r\n",
             host);

    received = ESP01_SendCommand(command,
                                 scratch,
                                 sizeof(scratch),
                                 15000U);

    connected =
        (strstr(scratch, "CONNECT") != NULL) ||
        (strstr(scratch, "Linked") != NULL) ||
        (strstr(scratch, "ALREADY CONNECTED") != NULL);

    if (!connected)
    {
        received = ESP01_SendCommand("AT+CIPSTATUS\r\n",
                                     scratch,
                                     sizeof(scratch),
                                     3000U);
        connected =
            (strstr(scratch, "STATUS:3") != NULL) ||
            (strstr(scratch, "\"TCP\"") != NULL);
    }

    if (!connected)
        return false;

    request_length = snprintf(
        request,
        sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: STM32F746-ESP01\r\n"
        "Accept: text/plain,application/json\r\n"
        "Accept-Language: en\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n",
        path,
        host);

    if ((request_length <= 0) ||
        (request_length >= (int)sizeof(request)) ||
        !ESP01_PrepareDataSend((uint16_t)request_length))
    {
        (void)ESP01_SendCommand("AT+CIPCLOSE\r\n",
                                scratch,
                                sizeof(scratch),
                                1000U);
        return false;
    }

    if (HAL_UART_Transmit(&huart1,
                          (uint8_t *)request,
                          (uint16_t)request_length,
                          5000U) != HAL_OK)
    {
        return false;
    }

    received = ESP01_ReadUntil(response,
                               response_capacity,
                               "CLOSED",
                               "ERROR",
                               timeout_ms);

    MCP2221A_Console_Print("\r\nHTTP RESPONSE:\r\n");
    MCP2221A_Console_Write(response, received);

    return ((strstr(response, "HTTP/1.1 200") != NULL) ||
            (strstr(response, "HTTP/1.0 200") != NULL));
}

static const char *HTTP_Body(const char *response)
{
    const char *body;

    if (response == NULL)
        return NULL;

    body = strstr(response, "\r\n\r\n");
    if (body == NULL)
        return NULL;

    body += 4;

    /* If transfer-encoding is chunked, skip the first hexadecimal size. */
    if (strstr(response, "ransfer-Encoding: chunked") != NULL)
    {
        const char *line_end = strstr(body, "\r\n");
        if (line_end != NULL)
            body = line_end + 2;
    }

    return body;
}

static int32_t Date_MonthNumber(const char *month)
{
    static const char *names[12] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (int32_t index = 0; index < 12; index++)
    {
        if (strncmp(month, names[index], 3U) == 0)
            return index + 1;
    }

    return 0;
}

static int64_t Date_DaysFromCivil(int32_t year,
                                  uint32_t month,
                                  uint32_t day)
{
    year -= (month <= 2U) ? 1 : 0;
    int32_t era = (year >= 0) ? (year / 400) : ((year - 399) / 400);
    uint32_t year_of_era = (uint32_t)(year - era * 400);
    uint32_t day_of_year =
        (153U * (month + ((month > 2U) ? (uint32_t)-3 : 9U)) + 2U) /
        5U + day - 1U;
    uint32_t day_of_era =
        year_of_era * 365U + year_of_era / 4U -
        year_of_era / 100U + day_of_year;

    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

static bool Clock_SyncFromHTTPDate(InternetWeather *weather,
                                   const char *response)
{
    const char *date;
    char weekday[4];
    char month_name[4];
    int day;
    int year;
    int hour;
    int minute;
    int second;
    int32_t month;
    int64_t epoch;

    if ((weather == NULL) || (response == NULL))
        return false;

    date = strstr(response, "Date:");
    if (date == NULL)
        date = strstr(response, "date:");

    if ((date == NULL) ||
        (sscanf(date,
                "%*[^:]: %3s, %d %3s %d %d:%d:%d",
                weekday,
                &day,
                month_name,
                &year,
                &hour,
                &minute,
                &second) != 7))
    {
        return false;
    }

    month = Date_MonthNumber(month_name);
    if ((month == 0) || (year < 2020) || (year > 2099) ||
        (day < 1) || (day > 31) ||
        (hour < 0) || (hour > 23) ||
        (minute < 0) || (minute > 59) ||
        (second < 0) || (second > 60))
    {
        return false;
    }

    epoch = Date_DaysFromCivil(year, (uint32_t)month, (uint32_t)day) *
            86400LL + hour * 3600LL + minute * 60LL + second;

    if ((epoch <= 0LL) || (epoch > 0xFFFFFFFFLL))
        return false;

    weather->utc_epoch_at_sync = (uint32_t)epoch;
    weather->sync_tick = HAL_GetTick();
    weather->clock_valid = true;
    return true;
}

static uint32_t Clock_LocalEpoch(const InternetWeather *weather)
{
    int64_t local_epoch;

    if ((weather == NULL) || !weather->clock_valid)
        return 0U;

    local_epoch =
        (int64_t)weather->utc_epoch_at_sync +
        (int64_t)((uint32_t)(HAL_GetTick() - weather->sync_tick) / 1000U) +
        weather->utc_offset_seconds;

    if ((local_epoch <= 0LL) || (local_epoch > 0xFFFFFFFFLL))
        return 0U;

    return (uint32_t)local_epoch;
}

static void Clock_CivilFromDays(int64_t days,
                                int32_t *year,
                                uint32_t *month,
                                uint32_t *day)
{
    days += 719468LL;
    int64_t era = (days >= 0LL) ?
        (days / 146097LL) : ((days - 146096LL) / 146097LL);
    uint32_t day_of_era = (uint32_t)(days - era * 146097LL);
    uint32_t year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) / 365U;
    int32_t calculated_year = (int32_t)year_of_era + (int32_t)era * 400;
    uint32_t day_of_year =
        day_of_era - (365U * year_of_era + year_of_era / 4U -
                      year_of_era / 100U);
    uint32_t month_part = (5U * day_of_year + 2U) / 153U;
    uint32_t calculated_day =
        day_of_year - (153U * month_part + 2U) / 5U + 1U;
    uint32_t calculated_month =
        month_part + ((month_part < 10U) ? 3U : (uint32_t)-9);

    calculated_year += (calculated_month <= 2U) ? 1 : 0;
    *year = calculated_year;
    *month = calculated_month;
    *day = calculated_day;
}

static void Clock_Format(const InternetWeather *weather,
                         char *time_text,
                         size_t time_capacity,
                         char *date_text,
                         size_t date_capacity,
                         uint16_t *minutes_today)
{
    static const char *weekdays[7] =
        {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *months[12] =
        {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    uint32_t epoch = Clock_LocalEpoch(weather);

    if ((time_text == NULL) || (date_text == NULL) || (epoch == 0U))
    {
        if ((time_text != NULL) && (time_capacity != 0U))
            snprintf(time_text, time_capacity, "--:--");
        if ((date_text != NULL) && (date_capacity != 0U))
            snprintf(date_text, date_capacity, "DATE WAITING");
        if (minutes_today != NULL)
            *minutes_today = 12U * 60U;
        return;
    }

    uint32_t second_of_day = epoch % 86400U;
    uint32_t hour = second_of_day / 3600U;
    uint32_t minute = (second_of_day % 3600U) / 60U;
    int64_t days = (int64_t)(epoch / 86400U);
    int32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t weekday = (uint32_t)((days + 4LL) % 7LL);

    Clock_CivilFromDays(days, &year, &month, &day);

    snprintf(time_text,
             time_capacity,
             "%02lu:%02lu",
             (unsigned long)hour,
             (unsigned long)minute);
    snprintf(date_text,
             date_capacity,
             "%s %02lu %s %04ld",
             weekdays[weekday],
             (unsigned long)day,
             months[month - 1U],
             (long)year);

    if (minutes_today != NULL)
        *minutes_today = (uint16_t)(hour * 60U + minute);
}

static APP_UNUSED bool JSON_CopyValue(const char *json,
                           const char *key,
                           char *destination,
                           size_t capacity)
{
    char pattern[48];
    const char *value;
    const char *end;
    size_t length;
    bool quoted;

    if ((json == NULL) || (key == NULL) ||
        (destination == NULL) || (capacity == 0U))
    {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    value = strstr(json, pattern);
    if (value == NULL)
        return false;

    value += strlen(pattern);
    while ((*value == ' ') || (*value == '\t'))
        value++;

    quoted = (*value == '"');
    if (quoted)
        value++;

    end = value;
    while (*end != '\0')
    {
        if ((quoted && (*end == '"')) ||
            (!quoted && ((*end == ',') || (*end == '}') ||
                         (*end == '\r') || (*end == '\n'))))
        {
            break;
        }
        end++;
    }

    length = (size_t)(end - value);
    if (length >= capacity)
        length = capacity - 1U;

    memcpy(destination, value, length);
    destination[length] = '\0';
    return length != 0U;
}

static bool Location_Fetch(InternetWeather *weather)
{
    if (weather == NULL)
        return false;

    snprintf(weather->city,
             sizeof(weather->city),
             "BRUSSELS");

    snprintf(weather->latitude,
             sizeof(weather->latitude),
             "50.8503");

    snprintf(weather->longitude,
             sizeof(weather->longitude),
             "4.3517");

    snprintf(weather->timezone,
             sizeof(weather->timezone),
             "Europe/Brussels");

    weather->utc_offset_seconds = 7200; /* UTC+2 */
    weather->location_valid = true;

    return true;
}
static bool Weather_ParseTemperature(const char *text, int16_t *value)
{
    char *end;
    long parsed;

    if ((text == NULL) || (value == NULL))
        return false;

    parsed = strtol(text, &end, 10);
    if ((end == text) || (parsed < -99L) || (parsed > 99L))
        return false;

    *value = (int16_t)parsed;
    return true;
}

static uint16_t Weather_ParseMinutes(const char *text)
{
    int hour = 0;
    int minute = 0;

    if ((text == NULL) ||
        (sscanf(text, "%d:%d", &hour, &minute) < 2))
    {
        return 0xFFFFU;
    }

    if (Text_ContainsInsensitive(text, "PM") && (hour < 12))
        hour += 12;
    if (Text_ContainsInsensitive(text, "AM") && (hour == 12))
        hour = 0;

    if ((hour < 0) || (hour > 23) ||
        (minute < 0) || (minute > 59))
    {
        return 0xFFFFU;
    }

    return (uint16_t)(hour * 60 + minute);
}

static void Weather_SelectTheme(InternetWeather *weather)
{
    char time_text[16];
    char date_text[32];
    uint16_t now_minutes;
    bool cloudy;

    if (weather == NULL)
        return;

    if (Text_ContainsInsensitive(weather->condition, "THUNDER") ||
        Text_ContainsInsensitive(weather->condition, "STORM"))
    {
        weather->theme = QSPI_WEATHER_STORM;
        return;
    }

    if (Text_ContainsInsensitive(weather->condition, "RAIN") ||
        Text_ContainsInsensitive(weather->condition, "DRIZZLE") ||
        Text_ContainsInsensitive(weather->condition, "SHOWER"))
    {
        weather->theme = QSPI_WEATHER_RAIN;
        return;
    }

    if (Text_ContainsInsensitive(weather->condition, "SNOW") ||
        Text_ContainsInsensitive(weather->condition, "SLEET") ||
        Text_ContainsInsensitive(weather->condition, "BLIZZARD"))
    {
        weather->theme = QSPI_WEATHER_SNOW;
        return;
    }

    cloudy =
        Text_ContainsInsensitive(weather->condition, "CLOUD") ||
        Text_ContainsInsensitive(weather->condition, "OVERCAST") ||
        Text_ContainsInsensitive(weather->condition, "FOG") ||
        Text_ContainsInsensitive(weather->condition, "MIST");

    Clock_Format(weather,
                 time_text,
                 sizeof(time_text),
                 date_text,
                 sizeof(date_text),
                 &now_minutes);

    if ((weather->dawn_minutes != 0xFFFFU) &&
        (weather->dusk_minutes != 0xFFFFU) &&
        ((now_minutes < weather->dawn_minutes) ||
         (now_minutes >= weather->dusk_minutes)))
    {
        weather->theme = QSPI_WEATHER_NIGHT;
    }
    else if ((weather->dawn_minutes != 0xFFFFU) &&
             (weather->sunrise_minutes != 0xFFFFU) &&
             (now_minutes >= weather->dawn_minutes) &&
             (now_minutes < (uint16_t)(weather->sunrise_minutes + 30U)))
    {
        weather->theme = QSPI_WEATHER_DAWN;
    }
    else if ((weather->sunset_minutes != 0xFFFFU) &&
             (weather->dusk_minutes != 0xFFFFU) &&
             (now_minutes + 45U >= weather->sunset_minutes) &&
             (now_minutes < weather->dusk_minutes))
    {
        weather->theme = QSPI_WEATHER_SUNSET;
    }
    else if (!weather->clock_valid)
    {
        weather->theme = cloudy ?
            QSPI_WEATHER_CLOUDY : QSPI_WEATHER_CLEAR_DAY;
    }
    else if ((now_minutes < 360U) || (now_minutes >= 1200U))
    {
        weather->theme = QSPI_WEATHER_NIGHT;
    }
    else if (now_minutes < 480U)
    {
        weather->theme = QSPI_WEATHER_DAWN;
    }
    else if (now_minutes >= 1080U)
    {
        weather->theme = QSPI_WEATHER_SUNSET;
    }
    else
    {
        weather->theme = cloudy ?
            QSPI_WEATHER_CLOUDY : QSPI_WEATHER_CLEAR_DAY;
    }
}

static APP_UNUSED bool Weather_ParseBody(InternetWeather *weather,
                              const char *body)
{
    char line[320];
    char *fields[11];
    size_t length = 0U;
    uint32_t field_count = 1U;
    char raw_condition[80];

    if ((weather == NULL) || (body == NULL))
        return false;

    /* ESP AT firmware may insert +IPD,<length>: before a packet, while an
     * HTTP proxy may insert a chunk-size line.  The first pipe ends the
     * temperature field; walk backwards to its nearest packet/line boundary. */
    const char *first_pipe = strchr(body, '|');
    if (first_pipe == NULL)
        return false;

    const char *field_start = first_pipe;
    while ((field_start > body) &&
           (field_start[-1] != ':') &&
           (field_start[-1] != '\r') &&
           (field_start[-1] != '\n'))
    {
        field_start--;
    }
    body = field_start;

    while ((body[length] != '\0') &&
           (body[length] != '\r') &&
           (body[length] != '\n') &&
           (length + 1U < sizeof(line)))
    {
        line[length] = body[length];
        length++;
    }
    line[length] = '\0';

    fields[0] = line;
    for (char *cursor = line;
         (*cursor != '\0') && (field_count < 11U);
         cursor++)
    {
        if (*cursor == '|')
        {
            *cursor = '\0';
            fields[field_count++] = cursor + 1;
        }
    }

    if (field_count != 11U)
        return false;

    if (!Weather_ParseTemperature(fields[0], &weather->temperature_c) ||
        !Weather_ParseTemperature(fields[1], &weather->feels_like_c) ||
        !Weather_ParseTemperature(fields[3], &weather->high_c) ||
        !Weather_ParseTemperature(fields[4], &weather->low_c))
    {
        return false;
    }

    snprintf(raw_condition, sizeof(raw_condition), "%s", fields[2]);
    Text_ToDisplayUpper(weather->condition,
                        sizeof(weather->condition),
                        raw_condition);

    weather->dawn_minutes = Weather_ParseMinutes(fields[5]);
    weather->sunrise_minutes = Weather_ParseMinutes(fields[6]);
    weather->sunset_minutes = Weather_ParseMinutes(fields[7]);
    weather->dusk_minutes = Weather_ParseMinutes(fields[8]);
    weather->weather_valid = true;
    weather->last_weather_success_tick = HAL_GetTick();
    Weather_SelectTheme(weather);
    return true;
}
static APP_UNUSED bool Weather_JSONRoundedInteger(const char *section,
                                       const char *key,
                                       int16_t *result)
{
    char pattern[64];
    const char *value;
    char *end;
    float number;

    if ((section == NULL) || (key == NULL) || (result == NULL))
        return false;

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    value = strstr(section, pattern);
    if (value == NULL)
        return false;

    value += strlen(pattern);

    while ((*value == ' ')  ||
           (*value == '\t') ||
           (*value == '\r') ||
           (*value == '\n'))
    {
        value++;
    }

    /* Handle one-element daily arrays such as [37.4]. */
    if (*value == '[')
    {
        value++;

        while ((*value == ' ')  ||
               (*value == '\t') ||
               (*value == '\r') ||
               (*value == '\n'))
        {
            value++;
        }
    }

    number = strtof(value, &end);

    if (end == value)
        return false;

    if ((number < -32768.0f) || (number > 32767.0f))
        return false;

    *result = (int16_t)((number >= 0.0f) ?
                       (number + 0.5f) :
                       (number - 0.5f));

    return true;
}


static APP_UNUSED bool Weather_JSONFirstString(const char *section,
                                    const char *key,
                                    char *destination,
                                    size_t capacity)
{
    char pattern[64];
    const char *value;
    const char *end;
    size_t length;

    if ((section == NULL) ||
        (key == NULL) ||
        (destination == NULL) ||
        (capacity == 0U))
    {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    value = strstr(section, pattern);
    if (value == NULL)
        return false;

    value += strlen(pattern);

    while ((*value == ' ')  ||
           (*value == '\t') ||
           (*value == '\r') ||
           (*value == '\n'))
    {
        value++;
    }

    /* Handle arrays such as ["2026-08-14T06:28"]. */
    if (*value == '[')
    {
        value++;

        while ((*value == ' ')  ||
               (*value == '\t') ||
               (*value == '\r') ||
               (*value == '\n'))
        {
            value++;
        }
    }

    if (*value != '"')
        return false;

    value++;
    end = strchr(value, '"');

    if (end == NULL)
        return false;

    length = (size_t)(end - value);

    if (length >= capacity)
        length = capacity - 1U;

    memcpy(destination, value, length);
    destination[length] = '\0';

    return length > 0U;
}


static const char *Weather_WMODescription(uint16_t code,
                                          bool is_day)
{
    if (code == 0U)
        return is_day ? "SUNNY" : "CLEAR NIGHT";

    if (code == 1U)
        return "MAINLY CLEAR";

    if (code == 2U)
        return "PARTLY CLOUDY";

    if (code == 3U)
        return "OVERCAST";

    if ((code == 45U) || (code == 48U))
        return "FOG";

    if ((code >= 51U) && (code <= 57U))
        return "DRIZZLE";

    if ((code >= 61U) && (code <= 67U))
        return "RAIN";

    if ((code >= 71U) && (code <= 77U))
        return "SNOW";

    if ((code >= 80U) && (code <= 82U))
        return "RAIN SHOWERS";

    if ((code == 85U) || (code == 86U))
        return "SNOW SHOWERS";

    if ((code >= 95U) && (code <= 99U))
        return "THUNDERSTORM";

    return "OUTSIDE WEATHER";
}

static bool Weather_Fetch(InternetWeather *weather)
{
    static char response[1024];

    const char *http_start;
    const char *body;
    char *end;
    long temperature;

    if (weather == NULL)
        return false;

    memset(response, 0, sizeof(response));

    if (!ESP01_HTTPGet("wttr.in",
                       "/Brussels?m&format=%t",
                       response,
                       sizeof(response),
                       25000U))
    {
        return false;
    }

    /* The HTTP Date header is still used for time/date. */
    (void)Clock_SyncFromHTTPDate(weather, response);

    /*
     * IMPORTANT:
     * Skip ESP8266 messages such as "Recv ...", "SEND OK" and "+IPD".
     * Search for the header terminator only after HTTP/1.x.
     */
    http_start = strstr(response, "HTTP/1.");

    if (http_start == NULL)
        return false;

    body = strstr(http_start, "\r\n\r\n");

    if (body != NULL)
    {
        body += 4;
    }
    else
    {
        /* Fallback for responses using LF without CR. */
        body = strstr(http_start, "\n\n");

        if (body == NULL)
            return false;

        body += 2;
    }

    /* Skip any whitespace before +32°C or -4°C. */
    while ((*body == ' ')  ||
           (*body == '\t') ||
           (*body == '\r') ||
           (*body == '\n'))
    {
        body++;
    }

    temperature = strtol(body, &end, 10);

    if ((end == body) ||
        (temperature < -80L) ||
        (temperature > 80L))
    {
        return false;
    }

    weather->temperature_c = (int16_t)temperature;
    weather->feels_like_c  = (int16_t)temperature;
    weather->high_c        = (int16_t)temperature;
    weather->low_c         = (int16_t)temperature;

    snprintf(weather->city,
             sizeof(weather->city),
             "BRUSSELS");

//   snprintf(weather->condition,
//             sizeof(weather->condition),
//             "");

    weather->location_valid = true;
    weather->weather_valid = true;
    weather->last_weather_success_tick = HAL_GetTick();

    Weather_SelectTheme(weather);

    return true;
}
static void GUI_ShowWeatherDashboard(bool scd_ok,
                                     bool bme_ok,
                                     bool wifi_ok,
                                     bool blynk_ok,
                                     const SensorValues *values,
                                     const InternetWeather *weather)
{
    static bool drawn = false;
    static SensorValues last_values;
    static InternetWeather last_weather;
    static bool last_scd_ok;
    static bool last_bme_ok;
    static bool last_wifi_ok;
    static bool last_blynk_ok;
    static char last_time[16] = "";
    static char last_date[32] = "";

    char time_text[16];
    char date_text[32];
    char outside_text[24];

    char room_temp_text[32];
    char humidity_text[32];
    char co2_text[32];
    char pressure_text[32];
    char air_text[40];

    const char *city_text;
    AirQualityLevel air_quality;

    uint16_t air_colour;
    uint16_t network_x;
    uint16_t air_line_width;
    uint16_t air_line_x;
    uint8_t city_scale;

    bool returning_from_alarm;
    bool force_update;

    if ((values == NULL) || (weather == NULL))
        return;

    Clock_Format(weather,
                 time_text,
                 sizeof(time_text),
                 date_text,
                 sizeof(date_text),
                 NULL);

    returning_from_alarm = gui_alarm_drawn;
    gui_alarm_drawn = false;
    gui_last_alarm = ALARM_NONE;

    force_update =
        !drawn ||
        returning_from_alarm ||
        (memcmp(&last_values, values, sizeof(*values)) != 0) ||
        (memcmp(&last_weather, weather, sizeof(*weather)) != 0) ||
        (last_scd_ok != scd_ok) ||
        (last_bme_ok != bme_ok) ||
        (last_wifi_ok != wifi_ok) ||
        (last_blynk_ok != blynk_ok) ||
        (strcmp(last_time, time_text) != 0) ||
        (strcmp(last_date, date_text) != 0);

    if (!force_update)
        return;

    lcd_background_theme = weather->theme;

    if (!LCD_LoadBackground())
        Error_Handler();

    /* ---------------------------------------------------------
     * Top status row: time, Wi-Fi, cloud and date
     * --------------------------------------------------------- */

    LCD_DrawStringShadow(10U,
                         4U,
                         time_text,
                         LCD_WHITE,
                         1U);

    network_x =
        10U +
        LCD_MeasureString(time_text, 1U) +
        9U;

    LCD_DrawIcon(network_x,
                 2U,
                 QSPI_ICON_WIFI,
                 wifi_ok ? LCD_CYAN : LCD_MUTED);

    network_x += QSPI_MODERN_ICON_WIDTH + 6U;

    LCD_DrawIcon(network_x,
                 2U,
                 QSPI_ICON_CLOUD,
                 blynk_ok ? LCD_GREEN : LCD_MUTED);

    LCD_DrawRightStringShadow(470U,
                              4U,
                              date_text,
                              LCD_WHITE,
                              1U);

    /* ---------------------------------------------------------
     * Outside weather
     * --------------------------------------------------------- */

    city_text = weather->location_valid ?
        weather->city : "LOCATING";

    city_scale =
        (LCD_MeasureString(city_text, 3U) < 460U) ?
        3U : 2U;

    LCD_DrawCenteredStringShadow(23U,
                                 "MY LOCATION",
                                 LCD_WHITE,
                                 1U);

    LCD_DrawCenteredStringShadow(36U,
                                 city_text,
                                 LCD_WHITE,
                                 city_scale);

    if (weather->weather_valid)
    {
        snprintf(outside_text,
                 sizeof(outside_text),
                 "%d C",
                 (int)weather->temperature_c);
    }
    else
    {
        snprintf(outside_text,
                 sizeof(outside_text),
                 "-- C");
    }

    LCD_DrawCenteredZoomShadow(60U,
                               outside_text,
                               LCD_WHITE,
                               QSPI_FONT_LARGE,
                               2U);

    /* ---------------------------------------------------------
     * Room measurements
     * --------------------------------------------------------- */

    LCD_DrawCenteredStringShadow(112U,
                                 "LIVING ROOM",
                                 LCD_CYAN,
                                 2U);

    if (scd_ok && values->room_environment_valid)
    {
        GUI_FormatSignedTenths(room_temp_text,
                               sizeof(room_temp_text),
                               values->room_temp10,
                               "C");

        snprintf(humidity_text,
                 sizeof(humidity_text),
                 "%lu.%lu %%",
                 (unsigned long)(values->room_rh10 / 10U),
                 (unsigned long)(values->room_rh10 % 10U));
    }
    else
    {
        snprintf(room_temp_text,
                 sizeof(room_temp_text),
                 "--.- C");

        snprintf(humidity_text,
                 sizeof(humidity_text),
                 "--.- %%");
    }

    if (bme_ok)
    {
        snprintf(pressure_text,
                 sizeof(pressure_text),
                 "%lu.%lu HPA",
                 (unsigned long)
                     (values->bme_pressure_hpa10 / 10U),
                 (unsigned long)
                     (values->bme_pressure_hpa10 % 10U));
    }
    else
    {
        snprintf(pressure_text,
                 sizeof(pressure_text),
                 "----.- HPA");
    }

    if (scd_ok)
    {
        snprintf(co2_text,
                 sizeof(co2_text),
                 "%u PPM",
                 (unsigned int)values->scd_co2);
    }
    else
    {
        snprintf(co2_text,
                 sizeof(co2_text),
                 "---- PPM");
    }

    /* Left column, first row: room temperature */

    LCD_DrawIcon(18U,
                 136U,
                 QSPI_ICON_TEMPERATURE,
                 LCD_ORANGE);

    LCD_DrawStringShadow(48U,
                         132U,
                         "TEMPERATURE",
                         LCD_MUTED,
                         1U);

    LCD_DrawStringShadow(48U,
                         148U,
                         room_temp_text,
                         LCD_WHITE,
                         2U);

    /* Right column, first row: CO2 */

    LCD_DrawIcon(250U,
                 136U,
                 QSPI_ICON_CO2,
                 LCD_CYAN);

    LCD_DrawStringShadow(280U,
                         132U,
                         "CARBON DIOXIDE",
                         LCD_MUTED,
                         1U);

    LCD_DrawStringShadow(280U,
                         148U,
                         co2_text,
                         LCD_WHITE,
                         2U);

    /* Left column, second row: humidity */

    LCD_DrawIcon(18U,
                 178U,
                 QSPI_ICON_HUMIDITY,
                 LCD_TEAL);

    LCD_DrawStringShadow(48U,
                         174U,
                         "HUMIDITY",
                         LCD_MUTED,
                         1U);

    LCD_DrawStringShadow(48U,
                         190U,
                         humidity_text,
                         LCD_WHITE,
                         2U);

    /* Right column, second row: pressure */

    LCD_DrawIcon(250U,
                 178U,
                 QSPI_ICON_PRESSURE,
                 LCD_BLUE);

    LCD_DrawStringShadow(280U,
                         174U,
                         "PRESSURE",
                         LCD_MUTED,
                         1U);

    LCD_DrawStringShadow(280U,
                         190U,
                         pressure_text,
                         LCD_WHITE,
                         2U);

    /* ---------------------------------------------------------
     * Air-quality status
     * --------------------------------------------------------- */

    air_quality = AirQuality_Get(scd_ok,
                                 bme_ok,
                                 values);

    air_colour = AirQuality_Colour(air_quality);

    snprintf(air_text,
             sizeof(air_text),
             "AIR QUALITY: %s",
             AirQuality_Name(air_quality));

    air_line_width =
        QSPI_MODERN_ICON_WIDTH +
        8U +
        LCD_MeasureString(air_text, 2U);

    air_line_x =
        (air_line_width < LCD_WIDTH) ?
        (uint16_t)((LCD_WIDTH - air_line_width) / 2U) :
        0U;

    LCD_DrawIcon(air_line_x,
                 219U,
                 QSPI_ICON_AIR_QUALITY,
                 air_colour);

    LCD_DrawStringShadow(
        (uint16_t)(air_line_x +
                   QSPI_MODERN_ICON_WIDTH +
                   8U),
        219U,
        air_text,
        air_colour,
        2U
    );

    LCD_DrawCenteredStringShadow(
        248U,
        AirQuality_Explanation(air_quality, values),
        LCD_WHITE,
        1U
    );

    LCD_PresentTextLayer();

    drawn = true;
    last_values = *values;
    last_weather = *weather;
    last_scd_ok = scd_ok;
    last_bme_ok = bme_ok;
    last_wifi_ok = wifi_ok;
    last_blynk_ok = blynk_ok;

    snprintf(last_time,
             sizeof(last_time),
             "%s",
             time_text);

    snprintf(last_date,
             sizeof(last_date),
             "%s",
             date_text);
}
static APP_UNUSED bool Blynk_UpdateVirtualPin(uint8_t virtual_pin,
                                   const char *value)
{
    static char command[160];
    static char request[512];
    static char response[1024];

    int request_length;
    uint16_t received;
    bool tcp_connected = false;
    bool http_ok = false;

    if (value == NULL)
    {
        LCD_ShowStatus("BLYNK",
                       "INVALID VALUE",
                       LCD_RED);

        return false;
    }

    LCD_ShowStatus("BLYNK",
                   "PREPARING TCP",
                   LCD_CYAN);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    /*
     * Close any previous connection.
     * ERROR is normal if no connection exists.
     */
    ESP01_SendCommand("AT+CIPCLOSE\r\n",
                      response,
                      sizeof(response),
                      1500U);

    HAL_Delay(300U);

    /* Use a single connection. */
    received = ESP01_SendCommand("AT+CIPMUX=0\r\n",
                                 response,
                                 sizeof(response),
                                 2000U);

    if ((received == 0U) ||
        (strstr(response, "OK") == NULL))
    {
        LCD_ShowStatus("BLYNK",
                       "CIPMUX FAILED",
                       LCD_RED);

        MCP2221A_Console_Print("\r\nCIPMUX RESPONSE:\r\n");
        MCP2221A_Console_Write(response, received);

        return false;
    }

    /* Ensure normal mode, not transparent transmission mode. */
    received = ESP01_SendCommand("AT+CIPMODE=0\r\n",
                                 response,
                                 sizeof(response),
                                 2000U);

    if ((received == 0U) ||
        (strstr(response, "OK") == NULL))
    {
        LCD_ShowStatus("BLYNK",
                       "CIPMODE FAILED",
                       LCD_RED);

        return false;
    }

    LCD_ShowStatus("BLYNK",
                   "SOCKET MODE OK",
                   LCD_GREEN);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    LCD_ShowStatus("BLYNK",
                   "OPENING TCP",
                   LCD_YELLOW);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    /*
     * Open an unencrypted TCP connection on port 80.
     * This is for testing with the old ESP-01 firmware.
     */
    snprintf(command,
             sizeof(command),
             "AT+CIPSTART=\"TCP\",\"%s\",80\r\n",
             BLYNK_SERVER);

    received = ESP01_SendCommand(command,
                                 response,
                                 sizeof(response),
                                 15000U);

    /*
     * Do not accept OK alone. A failed connection could return:
     *
     * CLOSED
     * OK
     */
    if ((strstr(response, "CONNECT") != NULL) ||
        (strstr(response, "Linked") != NULL) ||
        (strstr(response, "ALREADY CONNECTED") != NULL))
    {
        tcp_connected = true;
    }

    /*
     * Some firmware versions return OK before CONNECT.
     * Query the actual connection status.
     */
    if (!tcp_connected)
    {
        LCD_ShowStatus("BLYNK",
                       "CHECKING SOCKET",
                       LCD_CYAN);

        HAL_Delay(1000U);

        received = ESP01_SendCommand("AT+CIPSTATUS\r\n",
                                     response,
                                     sizeof(response),
                                     3000U);

        if ((strstr(response, "STATUS:3") != NULL) ||
            (strstr(response, "\"TCP\"") != NULL))
        {
            tcp_connected = true;
        }
    }

    if (!tcp_connected)
    {
        LCD_ShowStatus("BLYNK",
                       "TCP CONNECT FAIL",
                       LCD_RED);

        MCP2221A_Console_Print("\r\nTCP STATUS RESPONSE:\r\n");
        MCP2221A_Console_Write(response, received);

        return false;
    }

    LCD_ShowStatus("BLYNK",
                   "TCP CONNECTED",
                   LCD_GREEN);

    HAL_Delay(2000U);

    /*
     * Construct the Blynk HTTP request.
     *
     * Example:
     * GET /external/api/update?token=TOKEN&V0=123 HTTP/1.1
     */
    request_length = snprintf(
        request,
        sizeof(request),
        "GET /external/api/update?token=%s&V%u=%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: STM32F746-ESP01\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        BLYNK_AUTH_TOKEN,
        (unsigned int)virtual_pin,
        value,
        BLYNK_SERVER);

    if ((request_length <= 0) ||
        (request_length >= (int)sizeof(request)))
    {
        LCD_ShowStatus("BLYNK",
                       "REQUEST TOO LONG",
                       LCD_RED);

        ESP01_SendCommand("AT+CIPCLOSE\r\n",
                          response,
                          sizeof(response),
                          1000U);

        return false;
    }

    LCD_ShowStatus("BLYNK",
                   "REQUEST READY",
                   LCD_GREEN);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    LCD_ShowStatus("BLYNK",
                   "WAITING SEND",
                   LCD_YELLOW);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    /*
     * Send AT+CIPSEND and wait for the '>' prompt.
     */
    if (!ESP01_PrepareDataSend((uint16_t)request_length))
    {
        LCD_ShowStatus("BLYNK",
                       "NO SEND PROMPT",
                       LCD_RED);

        ESP01_SendCommand("AT+CIPCLOSE\r\n",
                          response,
                          sizeof(response),
                          1000U);

        return false;
    }

    /*
     * Show all display messages before transmitting.
     * Once transmission starts, USART1 must be read immediately.
     */
    LCD_ShowStatus("BLYNK",
                   "SENDING REQUEST",
                   LCD_YELLOW);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    LCD_ShowStatus("BLYNK",
                   "WAITING RESPONSE",
                   LCD_CYAN);

    HAL_Delay(BLYNK_SCREEN_DELAY_MS);

    /* Send the raw HTTP request. */
    if (HAL_UART_Transmit(&huart1,
                          (uint8_t *)request,
                          (uint16_t)request_length,
                          5000U) != HAL_OK)
    {
        LCD_ShowStatus("BLYNK",
                       "UART SEND FAIL",
                       LCD_RED);

        return false;
    }

    /*
     * IMPORTANT:
     * Read the ESP response immediately and exactly once.
     * Do not add a display operation or delay here.
     */
    received = ESP01_ReadUntil(response,
                               sizeof(response),
                               "CLOSED",
                               "ERROR",
                               15000U);

    /* Print the complete received response through USART3. */
    MCP2221A_Console_Print("\r\nBLYNK RESPONSE:\r\n");
    MCP2221A_Console_Write(response, received);

    /* Check for HTTP success. */
    http_ok =
        ((strstr(response, "HTTP/1.1 200") != NULL) ||
         (strstr(response, "HTTP/1.0 200") != NULL));

    if (http_ok)
    {
        LCD_ShowStatus("BLYNK",
                       "HTTP 200 OK",
                       LCD_GREEN);

        HAL_Delay(2000U);

        return true;
    }

    /* Show a more specific HTTP/network error. */
    if ((strstr(response, "HTTP/1.1 400") != NULL) ||
        (strstr(response, "HTTP/1.1 401") != NULL) ||
        (strstr(response, "HTTP/1.1 403") != NULL))
    {
        LCD_ShowStatus("BLYNK",
                       "TOKEN OR PIN ERROR",
                       LCD_RED);
    }
    else if ((strstr(response, "HTTP/1.1 301") != NULL) ||
             (strstr(response, "HTTP/1.1 302") != NULL) ||
             (strstr(response, "HTTP/1.1 307") != NULL) ||
             (strstr(response, "HTTP/1.1 308") != NULL))
    {
        LCD_ShowStatus("BLYNK",
                       "HTTP REDIRECT",
                       LCD_RED);
    }
    else if (received == 0U)
    {
        LCD_ShowStatus("BLYNK",
                       "NO RESPONSE",
                       LCD_RED);
    }
    else if (strstr(response, "SEND OK") == NULL)
    {
        LCD_ShowStatus("BLYNK",
                       "SEND FAILED",
                       LCD_RED);
    }
    else
    {
        LCD_ShowStatus("BLYNK",
                       "HTTP NO RESPONSE",
                       LCD_RED);
    }

    return false;
}
static bool BufferAppend(char *buffer,
                         size_t capacity,
                         size_t *used,
                         const char *format,
                         ...)
{
    va_list arguments;
    int written;
    size_t remaining;

    if ((buffer == NULL) ||
        (used == NULL) ||
        (format == NULL) ||
        (*used >= capacity))
    {
        return false;
    }

    remaining = capacity - *used;

    va_start(arguments, format);
    written = vsnprintf(&buffer[*used],
                        remaining,
                        format,
                        arguments);
    va_end(arguments);

    if ((written < 0) || ((size_t)written >= remaining))
        return false;

    *used += (size_t)written;
    return true;
}

static void FormatSignedTenthsValue(char *buffer,
                                    size_t capacity,
                                    int32_t value10)
{
    uint32_t absolute;

    if ((buffer == NULL) || (capacity == 0U))
        return;

    if (value10 < 0)
        absolute = (uint32_t)(-(int64_t)value10);
    else
        absolute = (uint32_t)value10;

    snprintf(buffer,
             capacity,
             "%s%lu.%lu",
             (value10 < 0) ? "-" : "",
             (unsigned long)(absolute / 10U),
             (unsigned long)(absolute % 10U));
}

static void FormatUnsignedTenthsValue(char *buffer,
                                      size_t capacity,
                                      uint32_t value10)
{
    if ((buffer == NULL) || (capacity == 0U))
        return;

    snprintf(buffer,
             capacity,
             "%lu.%lu",
             (unsigned long)(value10 / 10U),
             (unsigned long)(value10 % 10U));
}

static bool Blynk_UpdateAllMeasurements(bool scd_ok,
                                        bool bme_ok,
                                        bool flame_adc_ok,
                                        const SensorValues *values,
                                        const InternetWeather *weather)
{
    static char command[160];
    static char request[1024];
    static char response[1024];

    char scd_temperature[16];
    char scd_humidity[16];
    char bme_temperature[16];
    char bme_humidity[16];
    char bme_pressure[16];
    char gas_resistance[16];
    char flame_voltage[16];
    size_t request_length = 0U;
    uint16_t received;
    uint32_t gas_kohm10;
    bool tcp_connected = false;

    if (values == NULL)
        return false;

    request[0] = '\0';

    if (!BufferAppend(request,
                      sizeof(request),
                      &request_length,
                      "GET /external/api/batch/update?token=%s",
                      BLYNK_AUTH_TOKEN))
    {
        return false;
    }

    /* D0 is always available, even if the ADC conversion failed. */
    if (!BufferAppend(request,
                      sizeof(request),
                      &request_length,
                      "&V%u=%u",
                      VPIN_FLAME_DETECTED,
                      values->flame_detected ? 1U : 0U))
    {
        return false;
    }

    if (flame_adc_ok)
    {
        snprintf(flame_voltage,
                 sizeof(flame_voltage),
                 "%lu.%03lu",
                 (unsigned long)(values->flame_voltage_mv / 1000U),
                 (unsigned long)(values->flame_voltage_mv % 1000U));

        if (!BufferAppend(request,
                          sizeof(request),
                          &request_length,
                          "&V%u=%u&V%u=%s",
                          VPIN_FLAME_ADC,
                          (unsigned int)values->flame_adc_raw,
                          VPIN_FLAME_VOLTAGE,
                          flame_voltage))
        {
            return false;
        }
    }

    if (scd_ok)
    {
        FormatSignedTenthsValue(scd_temperature,
                                sizeof(scd_temperature),
                                values->room_temp10);
        FormatUnsignedTenthsValue(scd_humidity,
                                  sizeof(scd_humidity),
                                  values->room_rh10);

        if (!BufferAppend(request,
                          sizeof(request),
                          &request_length,
                          "&V%u=%u&V%u=%s&V%u=%s",
                          VPIN_CO2,
                          (unsigned int)values->scd_co2,
                          VPIN_SCD_TEMPERATURE,
                          scd_temperature,
                          VPIN_SCD_HUMIDITY,
                          scd_humidity))
        {
            return false;
        }
    }

    if (bme_ok)
    {
        FormatSignedTenthsValue(bme_temperature,
                                sizeof(bme_temperature),
                                values->bme_temp10);
        FormatUnsignedTenthsValue(bme_humidity,
                                  sizeof(bme_humidity),
                                  values->bme_rh10);
        FormatUnsignedTenthsValue(bme_pressure,
                                  sizeof(bme_pressure),
                                  values->bme_pressure_hpa10);

        gas_kohm10 = (values->bme_gas_ohms + 50U) / 100U;
        FormatUnsignedTenthsValue(gas_resistance,
                                  sizeof(gas_resistance),
                                  gas_kohm10);

        if (!BufferAppend(request,
                          sizeof(request),
                          &request_length,
                          "&V%u=%s&V%u=%s&V%u=%s"
                          "&V%u=%s&V%u=%lu&V%u=%u",
                          VPIN_BME_TEMPERATURE,
                          bme_temperature,
                          VPIN_BME_HUMIDITY,
                          bme_humidity,
                          VPIN_BME_PRESSURE,
                          bme_pressure,
                          VPIN_GAS_RESISTANCE,
                          gas_resistance,
                          VPIN_GAS_RATIO,
                          (unsigned long)values->bme_gas_ratio_percent,
                          VPIN_GAS_ALARM,
                          values->bme_gas_alarm ? 1U : 0U))
        {
            return false;
        }
    }

    if ((weather != NULL) && weather->weather_valid)
    {
        if (!BufferAppend(request,
                          sizeof(request),
                          &request_length,
                          "&V%u=%d&V%u=%d&V%u=%d&V%u=%d&V%u=%u",
                          VPIN_OUTSIDE_TEMP,
                          (int)weather->temperature_c,
                          VPIN_OUTSIDE_FEELS,
                          (int)weather->feels_like_c,
                          VPIN_OUTSIDE_HIGH,
                          (int)weather->high_c,
                          VPIN_OUTSIDE_LOW,
                          (int)weather->low_c,
                          VPIN_WEATHER_THEME,
                          (unsigned int)weather->theme))
        {
            return false;
        }
    }

    if (!BufferAppend(request,
                      sizeof(request),
                      &request_length,
                      " HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: STM32F746-ESP01\r\n"
                      "Accept: */*\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      BLYNK_SERVER))
    {
        return false;
    }

    /* Close an old socket, then force single-connection normal mode. */
    (void)ESP01_SendCommand("AT+CIPCLOSE\r\n",
                            response,
                            sizeof(response),
                            1500U);
    HAL_Delay(200U);

    received = ESP01_SendCommand("AT+CIPMUX=0\r\n",
                                 response,
                                 sizeof(response),
                                 2000U);

    if ((received == 0U) || (strstr(response, "OK") == NULL))
        return false;

    received = ESP01_SendCommand("AT+CIPMODE=0\r\n",
                                 response,
                                 sizeof(response),
                                 2000U);

    if ((received == 0U) || (strstr(response, "OK") == NULL))
        return false;

    snprintf(command,
             sizeof(command),
             "AT+CIPSTART=\"TCP\",\"%s\",80\r\n",
             BLYNK_SERVER);

    received = ESP01_SendCommand(command,
                                 response,
                                 sizeof(response),
                                 15000U);

    if ((strstr(response, "CONNECT") != NULL) ||
        (strstr(response, "Linked") != NULL) ||
        (strstr(response, "ALREADY CONNECTED") != NULL))
    {
        tcp_connected = true;
    }

    if (!tcp_connected)
    {
        HAL_Delay(500U);

        received = ESP01_SendCommand("AT+CIPSTATUS\r\n",
                                     response,
                                     sizeof(response),
                                     3000U);

        if ((strstr(response, "STATUS:3") != NULL) ||
            (strstr(response, "\"TCP\"") != NULL))
        {
            tcp_connected = true;
        }
    }

    if (!tcp_connected)
        return false;

    if (!ESP01_PrepareDataSend((uint16_t)request_length))
        return false;

    if (HAL_UART_Transmit(&huart1,
                          (uint8_t *)request,
                          (uint16_t)request_length,
                          5000U) != HAL_OK)
    {
        return false;
    }

    /* Read immediately after the raw request; do not add a delay here. */
    received = ESP01_ReadUntil(response,
                               sizeof(response),
                               "CLOSED",
                               "ERROR",
                               15000U);

    MCP2221A_Console_Print("\r\nBLYNK BATCH RESPONSE:\r\n");
    MCP2221A_Console_Write(response, received);

    return ((strstr(response, "HTTP/1.1 200") != NULL) ||
            (strstr(response, "HTTP/1.0 200") != NULL));
}
static APP_UNUSED void Weather_DebugRawResponse(void)
{
    static char response[4096];
    char line[64];
    const char *cursor;
    uint32_t page;
    uint32_t delay_step;
    uint16_t row;
    uint16_t used;
    bool request_ok;

    memset(response, 0, sizeof(response));

    /*
     * Direct test: independent of location detection and JSON parsing.
     * Expected body: something like +38°C
     */
    request_ok =
        ESP01_HTTPGet("wttr.in",
                      "/Brussels?m&format=%t",
                      response,
                      sizeof(response),
                      25000U);

    /*
     * If the HTTP function failed before receiving anything, make that
     * visible instead of displaying an empty screen.
     */
    if (response[0] == '\0')
    {
        snprintf(response,
                 sizeof(response),
                 "NO HTTP RESPONSE RECEIVED\n"
                 "ESP01_HTTPGet RETURNED: %s\n"
                 "FAILURE HAPPENED BEFORE HTTP DATA\n"
                 "POSSIBLE DNS, CIPSTART OR CIPSEND FAILURE",
                 request_ok ? "TRUE" : "FALSE");
    }

    /*
     * Stay permanently inside this diagnostic viewer.
     * Long responses are displayed page by page and then repeated.
     */
    while (1)
    {
        cursor = response;
        page = 1U;

        do
        {
            LCD_ClearTextLayer(LCD_BLACK);

            snprintf(line,
                     sizeof(line),
                     "RAW HTTP %s  PAGE %lu",
                     request_ok ? "OK" : "FAIL",
                     (unsigned long)page);

            LCD_DrawString(0U,
                           0U,
                           line,
                           request_ok ? LCD_GREEN : LCD_RED,
                           1U);

            row = 0U;

            while ((*cursor != '\0') && (row < 28U))
            {
                used = 0U;

                /*
                 * Put a maximum of 58 characters on each display line.
                 */
                while ((*cursor != '\0') &&
                       (*cursor != '\n') &&
                       (used < 58U))
                {
                    unsigned char character =
                        (unsigned char)*cursor++;

                    if (character == '\r')
                        continue;

                    /*
                     * Replace UTF-8/binary characters with dots.
                     * The actual numeric temperature will remain readable.
                     */
                    if ((character >= 32U) && (character <= 126U))
                        line[used++] = (char)character;
                    else
                        line[used++] = '.';
                }

                if (*cursor == '\n')
                    cursor++;

                line[used] = '\0';

                LCD_DrawString(0U,
                               (uint16_t)(12U + row * 9U),
                               line,
                               LCD_WHITE,
                               1U);

                row++;
            }

            LCD_PresentTextLayer();

            /*
             * Keep each page visible for six seconds.
             * Flame detection remains active during the wait.
             */
            for (delay_step = 0U; delay_step < 60U; delay_step++)
            {
                (void)Safety_FlameActiveDuringNetwork();
                HAL_Delay(100U);
            }

            page++;

        } while (*cursor != '\0');
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* Re-assert the protected SDRAM and QSPI MPU regions after HAL reset. */
  MPU_FixExternalMemory();

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_LTDC_Init();
  MX_USART1_UART_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_I2C4_Init();
  MX_USART3_UART_Init();
  MCP2221A_Console_Init(&huart3, App_ConsoleCommand);
  MCP2221A_Console_PrintBanner();
  MX_FMC_Init();
  MX_QUADSPI_Init();
  /* USER CODE BEGIN 2 */

  bool qspi_assets_installed_now = false;

  /* SDRAM must be awake before either GUI framebuffer is touched. */
  if (SDRAM_Startup() != HAL_OK)
  {
      MCP2221A_Console_Print("[FAIL] SDRAM startup\r\n");
      Error_Handler();
  }

  app_runtime.sdram_ok = true;
  MCP2221A_Console_Print("[PASS] SDRAM startup\r\n");
  MCP2221A_Console_Print("[....] QSPI asset initialization\r\n");

  if (QSPI_Assets_Init(&hqspi, &qspi_assets_installed_now) != HAL_OK)
  {
      char qspi_error_line[40];
      int qspi_error_length = snprintf(
          qspi_error_line,
          sizeof(qspi_error_line),
          "QSPI ASSET ERROR %u\r\n",
          (unsigned int)QSPI_Assets_LastError()
      );

      if (qspi_error_length > 0)
      {
          MCP2221A_Console_Write(qspi_error_line,
                                 (size_t)qspi_error_length);
      }

      Error_Handler();
  }

  app_runtime.qspi_ok = true;
  MCP2221A_Console_Printf("[PASS] QSPI assets %s\r\n",
                          qspi_assets_installed_now ?
                              "installed" : "ready");

  /* Configure LTDC only after SDRAM is ready and QSPI is memory mapped. */
  LCD_ConfigureTextLayer();
  LCD_ShowStatus("QSPI ASSETS",
                 qspi_assets_installed_now ? "INSTALLED" : "READY",
                 LCD_GREEN);
  HAL_Delay(qspi_assets_installed_now ? 2000U : 500U);

  /* The separate diagnostic firmware already verified all 8 MiB. Never run
   * a destructive SDRAM/QSPI test after GUI memory is live. */
  bool scd41_online;
  bool bme688_online;
  bool scd41_read_ok = false;
  bool bme688_read_ok = false;
  bool flame_adc_ok = false;
  bool scd41_has_sample = false;
  bool bme688_has_sample = false;
  bool flame_has_sample = false;
  bool scd41_healthy = false;
  bool bme688_healthy = false;
  bool flame_sensor_healthy = false;
  bool measurements_ready = false;
  bool esp_ok;
  bool wifi_ok = false;
  bool blynk_ok = false;
  uint8_t scd41_failures = 0U;
  uint8_t bme688_failures = 0U;
  uint8_t flame_failures = 0U;
  SystemAlarm current_alarm = {ALARM_NONE, BUZZER_OFF};
  int8_t last_uploaded_flame = -1;
  uint32_t now;
  uint32_t last_flame_tick;
  uint32_t last_sensor_tick;
  uint32_t last_display_tick;
  uint32_t last_blynk_success_tick;
  uint32_t last_blynk_attempt_tick;
  uint32_t last_wifi_retry_tick;
  uint32_t last_sensor_retry_tick;
  uint32_t last_location_attempt_tick;
  uint32_t last_weather_attempt_tick;

  LCD_ShowStatus("INITIALIZING",
                 "SCD41 AND BME688",
                 LCD_CYAN);

  scd41_online = SCD41_Init();
  bme688_online = BME688_Init();

  app_runtime.scd41_ok = scd41_online;
  app_runtime.bme688_ok = bme688_online;
  MCP2221A_Console_Printf("[%s] SCD41 on I2C4\r\n",
                          scd41_online ? "PASS" : "FAIL");
  if (scd41_online)
  {
      MCP2221A_Console_Printf(
          "[INFO] SCD41 integration offset=%u.%u C; room T/RH source=SCD41\r\n",
          (unsigned int)(SCD41_TEMPERATURE_OFFSET10 / 10U),
          (unsigned int)(SCD41_TEMPERATURE_OFFSET10 % 10U));
  }
  MCP2221A_Console_Printf("[%s] BME688 on I2C3\r\n",
                          bme688_online ? "PASS" : "FAIL");

  if (!scd41_online)
      scd41_failures = SENSOR_FAILURE_LIMIT;

  if (!bme688_online)
      bme688_failures = SENSOR_FAILURE_LIMIT;

  LCD_ShowStatus("INITIALIZING",
                 "ESP-01 WIFI",
                 LCD_CYAN);

  esp_ok = ESP01_TestScreen();
  app_runtime.esp_ok = esp_ok;
  MCP2221A_Console_Printf("[%s] ESP-01 AT interface on USART1\r\n",
                          esp_ok ? "PASS" : "FAIL");

  if (esp_ok)
  {
      HAL_Delay(1000U);
      wifi_ok = ESP01_ConnectWiFi();
      app_runtime.wifi_ok = wifi_ok;
      MCP2221A_Console_Printf("[%s] Wi-Fi connection\r\n",
                              wifi_ok ? "PASS" : "FAIL");
//      if (wifi_ok)
//      {
//          Weather_DebugRawResponse();
//      }
      if (wifi_ok)
      {
          LCD_ShowStatus("MY LOCATION",
                         "DETECTING...",
                         LCD_CYAN);

          if (Location_Fetch(&internet_weather))
          {
              LCD_ShowStatus("OUTSIDE WEATHER",
                             "DOWNLOADING...",
                             LCD_CYAN);
              (void)Weather_Fetch(&internet_weather);
          }
      }
  }

  now = HAL_GetTick();
  last_flame_tick = now - FLAME_SAMPLE_PERIOD_MS;
  last_sensor_tick = now - SENSOR_SAMPLE_PERIOD_MS;
  last_display_tick = now - DISPLAY_REFRESH_MS;
  last_blynk_success_tick = now - BLYNK_UPLOAD_PERIOD_MS;
  last_blynk_attempt_tick = now - BLYNK_RETRY_PERIOD_MS;
  last_wifi_retry_tick = now;
  last_sensor_retry_tick = now;
  last_location_attempt_tick = now;
  last_weather_attempt_tick = now;

  /* The GPIO default is already low, but repeat it here so no previous test
   * state can leave the active buzzer enabled. */
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);

  /* From this point onward network debug functions may print to USART3, but
   * they may not replace the live dashboard on the LCD. */
  gui_dashboard_active = true;

  MCP2221A_Console_Print("[READY] Home HUB application running\r\n> ");


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      bool upload_due;
      bool flame_changed;
      bool network_request_made = false;

      MCP2221A_Console_Process();
      now = HAL_GetTick();

      /* Fast local flame sampling. */
      if ((uint32_t)(now - last_flame_tick) >=
          FLAME_SAMPLE_PERIOD_MS)
      {
          last_flame_tick = now;
          flame_adc_ok = FlameSensor_ReadAll(&sensor_values);

          if (flame_adc_ok)
          {
              flame_has_sample = true;
              flame_failures = 0U;
          }
          else if (flame_failures < SENSOR_FAILURE_LIMIT)
          {
              flame_failures++;
          }
      }

      /* Environmental sensors: the SCD41 produces one sample per 5 s. */
      if ((uint32_t)(now - last_sensor_tick) >=
          SENSOR_SAMPLE_PERIOD_MS)
      {
          last_sensor_tick = now;

          scd41_read_ok =
              scd41_online && SCD41_Read(&sensor_values);

          bme688_read_ok =
              bme688_online && BME688_Read(&sensor_values);

          if (scd41_read_ok)
          {
              scd41_has_sample = true;
              scd41_failures = 0U;
          }
          else if (scd41_failures < SENSOR_FAILURE_LIMIT)
          {
              scd41_failures++;
          }

          if (bme688_read_ok)
          {
              bme688_has_sample = true;
              bme688_failures = 0U;
          }
          else if (bme688_failures < SENSOR_FAILURE_LIMIT)
          {
              bme688_failures++;
          }

          if (bme688_read_ok)
              GasMonitor_Update(&sensor_values);

          measurements_ready = true;
      }

      scd41_healthy =
          scd41_online && scd41_has_sample &&
          (scd41_failures < SENSOR_FAILURE_LIMIT);

      bme688_healthy =
          bme688_online && bme688_has_sample &&
          (bme688_failures < SENSOR_FAILURE_LIMIT);

      flame_sensor_healthy =
          flame_has_sample &&
          (flame_failures < SENSOR_FAILURE_LIMIT);

      /* A disconnected I2C sensor is retried without rebooting the hub. */
      if (((!scd41_healthy) || (!bme688_healthy)) &&
          ((uint32_t)(now - last_sensor_retry_tick) >=
           SENSOR_RETRY_PERIOD_MS))
      {
          last_sensor_retry_tick = now;

          if (!scd41_healthy)
          {
              scd41_online = SCD41_Init();

              if (scd41_online)
              {
                  scd41_failures = 0U;
                  last_sensor_tick =
                      HAL_GetTick() - SENSOR_SAMPLE_PERIOD_MS;
              }
          }

          if (!bme688_healthy)
          {
              bme688_online = BME688_Init();

              if (bme688_online)
              {
                  bme688_failures = 0U;
                  last_sensor_tick =
                      HAL_GetTick() - SENSOR_SAMPLE_PERIOD_MS;
              }
          }

          now = HAL_GetTick();
      }

      current_alarm =
          SystemAlarm_Evaluate(measurements_ready,
                               scd41_healthy,
                               bme688_healthy,
                               flame_sensor_healthy,
                               &sensor_values);

      app_runtime.esp_ok = esp_ok;
      app_runtime.wifi_ok = wifi_ok;
      app_runtime.blynk_ok = blynk_ok;
      app_runtime.scd41_ok = scd41_healthy;
      app_runtime.bme688_ok = bme688_healthy;
      app_runtime.flame_sensor_ok = flame_sensor_healthy;
      app_runtime.alarm = current_alarm.type;

      /* Local display and buzzer always have priority over Wi-Fi traffic. */
      Buzzer_Update(current_alarm.buzzer, now);

      if ((uint32_t)(now - last_display_tick) >=
          DISPLAY_REFRESH_MS)
      {
          last_display_tick = now;
          Weather_SelectTheme(&internet_weather);

          if (current_alarm.type != ALARM_NONE)
          {
              /* Static colored alarm page: no intentional flashing. */
              GUI_ShowAlarm(current_alarm, true, &sensor_values);
          }
          else
          {
              GUI_ShowWeatherDashboard(scd41_healthy,
                                       bme688_healthy,
                                       wifi_ok,
                                       blynk_ok,
                                       &sensor_values,
                                       &internet_weather);
          }
      }

      /* Retry Wi-Fi periodically if it was unavailable at startup. */
      if ((current_alarm.type == ALARM_NONE) &&
          !wifi_ok && esp_ok &&
          ((uint32_t)(now - last_wifi_retry_tick) >=
           WIFI_RETRY_PERIOD_MS))
      {
          last_wifi_retry_tick = now;
          wifi_ok = ESP01_ConnectWiFi();
          now = HAL_GetTick();

          if (wifi_ok)
          {
              last_location_attempt_tick = now - LOCATION_RETRY_MS;
              last_weather_attempt_tick = now - WEATHER_RETRY_MS;
          }

          network_request_made = true;
      }

      /* The recovered final Location_Fetch() uses fixed Brussels coordinates
       * and a fixed UTC+2 offset. This periodic call does not update DST. */
      if ((current_alarm.type == ALARM_NONE) &&
          wifi_ok && !network_request_made &&
          ((uint32_t)(now - last_location_attempt_tick) >=
           (internet_weather.location_valid ?
                LOCATION_REFRESH_MS : LOCATION_RETRY_MS)))
      {
          last_location_attempt_tick = now;
          network_request_made = true;

          if (Location_Fetch(&internet_weather))
          {
              now = HAL_GetTick();
              last_weather_attempt_tick = now - WEATHER_REFRESH_MS;
          }
          else
          {
              now = HAL_GetTick();
          }
      }

      if ((current_alarm.type == ALARM_NONE) &&
          wifi_ok && internet_weather.location_valid &&
          !network_request_made &&
          ((uint32_t)(now - last_weather_attempt_tick) >=
           (internet_weather.weather_valid ?
                WEATHER_REFRESH_MS : WEATHER_RETRY_MS)))
      {
          last_weather_attempt_tick = now;
          network_request_made = true;
          (void)Weather_Fetch(&internet_weather);
          now = HAL_GetTick();
      }

      upload_due =
          ((uint32_t)(now - last_blynk_success_tick) >=
           BLYNK_UPLOAD_PERIOD_MS);

      flame_changed =
          (last_uploaded_flame !=
           (int8_t)(sensor_values.flame_detected ? 1 : 0));

      /*
       * Upload everything in one HTTP batch. A flame transition is sent
       * immediately; normal measurements are sent every 10 seconds.
       */
      if ((current_alarm.type == ALARM_NONE) &&
          wifi_ok && measurements_ready && !network_request_made &&
          (upload_due || flame_changed) &&
          ((uint32_t)(now - last_blynk_attempt_tick) >=
           BLYNK_RETRY_PERIOD_MS))
      {
          last_blynk_attempt_tick = now;

          blynk_ok =
              Blynk_UpdateAllMeasurements(scd41_read_ok,
                                          bme688_read_ok,
                                          flame_adc_ok,
                                          &sensor_values,
                                          &internet_weather);

          now = HAL_GetTick();

          if (blynk_ok)
          {
              last_blynk_success_tick = now;
              last_uploaded_flame =
                  sensor_values.flame_detected ? 1 : 0;
          }

          app_runtime.blynk_ok = blynk_ok;
      }

      /* Network functions can block briefly; restore the correct buzzer state
       * immediately after they return. */
      now = HAL_GetTick();
      Buzzer_Update(sensor_values.flame_detected ?
                    BUZZER_CRITICAL : current_alarm.buzzer,
                    now);

      MCP2221A_Console_Process();
      HAL_Delay(10U);

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  /* The board uses a powered ASCO 8 MHz oscillator on OSC_IN. */
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 200;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  /* Longer acquisition time gives a more stable PA1 reading. */
  sConfig.SamplingTime = ADC_SAMPLETIME_56CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00C0EAFF;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x00C0EAFF;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief I2C4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C4_Init(void)
{

  /* USER CODE BEGIN I2C4_Init 0 */

  /* USER CODE END I2C4_Init 0 */

  /* USER CODE BEGIN I2C4_Init 1 */

  /* USER CODE END I2C4_Init 1 */
  hi2c4.Instance = I2C4;
  hi2c4.Init.Timing = 0x00C0EAFF;
  hi2c4.Init.OwnAddress1 = 0;
  hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c4.Init.OwnAddress2 = 0;
  hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C4_Init 2 */

  /* USER CODE END I2C4_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};
  LTDC_LayerCfgTypeDef pLayerCfg1 = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 7;
  hltdc.Init.VerticalSync = 3;
  hltdc.Init.AccumulatedHBP = 14;
  hltdc.Init.AccumulatedVBP = 5;
  hltdc.Init.AccumulatedActiveW = 654;
  hltdc.Init.AccumulatedActiveH = 485;
  hltdc.Init.TotalWidth = 660;
  hltdc.Init.TotalHeigh = 487;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 0;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 0;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
  pLayerCfg.Alpha = 0;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = 0;
  pLayerCfg.ImageWidth = 0;
  pLayerCfg.ImageHeight = 0;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg1.WindowX0 = 0;
  pLayerCfg1.WindowX1 = 0;
  pLayerCfg1.WindowY0 = 0;
  pLayerCfg1.WindowY1 = 0;
  pLayerCfg1.PixelFormat = LTDC_PIXEL_FORMAT_ARGB8888;
  pLayerCfg1.Alpha = 0;
  pLayerCfg1.Alpha0 = 0;
  pLayerCfg1.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg1.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg1.FBStartAdress = 0;
  pLayerCfg1.ImageWidth = 0;
  pLayerCfg1.ImageHeight = 0;
  pLayerCfg1.Backcolor.Blue = 0;
  pLayerCfg1.Backcolor.Green = 0;
  pLayerCfg1.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg1, 1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* Enforce the 480x272 geometry even after CubeMX regeneration. */
  hltdc.Init.AccumulatedActiveW = 494;
  hltdc.Init.AccumulatedActiveH = 277;
  hltdc.Init.TotalWidth = 500;
  hltdc.Init.TotalHeigh = 279;

  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }

  /* The framebuffer layers are configured after QSPI enters memory-mapped
   * mode in USER CODE BEGIN 2. */

  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief QUADSPI Initialization Function
  * @param None
  * @retval None
  */
static void MX_QUADSPI_Init(void)
{

  /* USER CODE BEGIN QUADSPI_Init 0 */

  /* USER CODE END QUADSPI_Init 0 */

  /* USER CODE BEGIN QUADSPI_Init 1 */

  /* USER CODE END QUADSPI_Init 1 */
  /* QUADSPI parameter configuration*/
  hqspi.Instance = QUADSPI;
  /* About 25 MHz with the existing 200 MHz clock. LTDC now scans the L8
   * framebuffer in SRAM, so QSPI no longer needs continuous video bandwidth. */
  hqspi.Init.ClockPrescaler = 7;
  hqspi.Init.FifoThreshold = 1;
  hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.FlashSize = 23;
  hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_4_CYCLE;
  hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
  hqspi.Init.FlashID = QSPI_FLASH_ID_1;
  hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN QUADSPI_Init 2 */

  /* USER CODE END QUADSPI_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/* FMC initialization function */
static void MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */

  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  /* This exact 66.667-MHz configuration passed repeated full 8-MiB tests. */
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_3;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 8;
  SdramTiming.SelfRefreshTime = 5;
  SdramTiming.RowCycleDelay = 7;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */

  /* USER CODE END FMC_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : FLAME_D0_Pin */
  GPIO_InitStruct.Pin = FLAME_D0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(FLAME_D0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : butt1_Pin butt2_Pin butt3_Pin */
  GPIO_InitStruct.Pin = butt1_Pin|butt2_Pin|butt3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

  /*Configure GPIO pin : BUZZER_Pin */
  GPIO_InitStruct.Pin = BUZZER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZZER_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_BL_CTRL_Pin */
  GPIO_InitStruct.Pin = LCD_BL_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_BL_CTRL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_DISP_Pin */
  GPIO_InitStruct.Pin = LCD_DISP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_DISP_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  HAL_GPIO_WritePin(LCD_DISP_GPIO_Port,
                    LCD_DISP_Pin,
                    GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port,
                    LCD_BL_CTRL_Pin,
                    GPIO_PIN_SET);
  HAL_Delay(20U);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0xA0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0xC0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
