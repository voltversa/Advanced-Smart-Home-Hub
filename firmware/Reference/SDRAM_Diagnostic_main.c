/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F746 + IS42S16400J dedicated SDRAM diagnostic
  ******************************************************************************
  *
  * This program intentionally initializes only:
  *   - GPIO required for the LCD enable/backlight
  *   - LTDC, using an INTERNAL-SRAM framebuffer
  *   - USART3, for an optional 115200-baud diagnostic log
  *   - FMC SDRAM bank 1
  *
  * It does not initialize QSPI, Wi-Fi, I2C sensors, ADC, touch, or the GUI.
  * The SDRAM is tested continuously until the hardware works reliably.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Board configuration                                                        */
/* -------------------------------------------------------------------------- */

#define LCD_WIDTH                       480U
#define LCD_HEIGHT                      272U

/* Small RGB565 framebuffer in INTERNAL SRAM. SDRAM is never used by LTDC. */
#define TEXT_WIDTH                      320U
#define TEXT_HEIGHT                     128U
#define TEXT_X                          ((LCD_WIDTH - TEXT_WIDTH) / 2U)
#define TEXT_Y                          ((LCD_HEIGHT - TEXT_HEIGHT) / 2U)

#define LCD_BLACK                       0x0000U
#define LCD_WHITE                       0xFFFFU
#define LCD_RED                         0xF800U
#define LCD_GREEN                       0x07E0U
#define LCD_CYAN                        0x07FFU
#define LCD_YELLOW                      0xFFE0U
#define LCD_DARK_BLUE                   0x0010U

/* IS42S16400J: 4M x 16 = 8 MiB, FMC SDRAM bank 1. */
#define SDRAM_BASE_ADDRESS              0xC0000000UL
#define SDRAM_SIZE_BYTES                (8UL * 1024UL * 1024UL)
#define SDRAM_HALFWORD_COUNT            (SDRAM_SIZE_BYTES / 2UL)

/*
 * Conservative diagnostic clock:
 *   HCLK 200 MHz / 3 = 66.667 MHz SDRAM clock.
 *   Refresh = (64 ms / 4096 rows) * 66.667 MHz - 20 = approximately 1022.
 *
 * Leave this set to 1 until every test repeatedly passes. For a later
 * 100-MHz test, set it to 0 and rebuild.
 */
#define SDRAM_DIAGNOSTIC_SLOW_CLOCK     1U

#if SDRAM_DIAGNOSTIC_SLOW_CLOCK
#define SDRAM_CLOCK_LABEL               "66MHZ"
#define SDRAM_CLOCK_PERIOD              FMC_SDRAM_CLOCK_PERIOD_3
#define SDRAM_REFRESH_COUNT             1022U
#define SDRAM_READ_BURST                FMC_SDRAM_RBURST_DISABLE
#define SDRAM_READ_PIPE                 FMC_SDRAM_RPIPE_DELAY_0
#else
#define SDRAM_CLOCK_LABEL               "100MHZ"
#define SDRAM_CLOCK_PERIOD              FMC_SDRAM_CLOCK_PERIOD_2
#define SDRAM_REFRESH_COUNT             1542U
#define SDRAM_READ_BURST                FMC_SDRAM_RBURST_ENABLE
#define SDRAM_READ_PIPE                 FMC_SDRAM_RPIPE_DELAY_1
#endif

#define SDRAM_MODEREG_BURST_LENGTH_1          0x0000U
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   0x0000U
#define SDRAM_MODEREG_CAS_LATENCY_3            0x0030U
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD  0x0000U
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE   0x0200U

/* -------------------------------------------------------------------------- */
/* HAL handles                                                                */
/* -------------------------------------------------------------------------- */

LTDC_HandleTypeDef hltdc;
UART_HandleTypeDef huart3;
SDRAM_HandleTypeDef hsdram1;

/* -------------------------------------------------------------------------- */
/* Diagnostic state                                                           */
/* -------------------------------------------------------------------------- */

typedef enum
{
    SDRAM_STAGE_STARTUP = 0,
    SDRAM_STAGE_DATA_BUS,
    SDRAM_STAGE_BYTE_MASK,
    SDRAM_STAGE_WORD32,
    SDRAM_STAGE_ADDRESS_BUS,
    SDRAM_STAGE_FULL_MEMORY,
    SDRAM_STAGE_PASS
} SDRAM_TestStage;

typedef struct
{
    SDRAM_TestStage stage;
    uint32_t address;
    uint32_t expected;
    uint32_t actual_first;
    uint32_t actual_second;
} SDRAM_TestResult;

static uint32_t test_cycle = 0U;
static uint32_t failure_count = 0U;

/* 320 x 128 x 2 = 81,920 bytes of internal SRAM. */
__attribute__((aligned(32)))
static uint16_t text_framebuffer[TEXT_WIDTH * TEXT_HEIGHT];

/* Characters supported by the diagnostic screen. */
static const char font_characters[] =
    " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!-.:?";

static const uint8_t font5x7[][7] =
{
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},

    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, /* 5 */
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}, /* 9 */

    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */

    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, /* : */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}  /* ? */
};

/* -------------------------------------------------------------------------- */
/* Function prototypes                                                        */
/* -------------------------------------------------------------------------- */

void SystemClock_Config(void);
void Error_Handler(void);

static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_LTDC_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_FMC_Init(void);

static HAL_StatusTypeDef SDRAM_Startup(void);
static bool SDRAM_RunAllTests(SDRAM_TestResult *result);

/* -------------------------------------------------------------------------- */
/* Small LCD text renderer                                                    */
/* -------------------------------------------------------------------------- */

static const uint8_t *LCD_GetGlyph(char character)
{
    uint32_t index;

    if ((character >= 'a') && (character <= 'z'))
    {
        character = (char)(character - ('a' - 'A'));
    }

    for (index = 0U; index < (sizeof(font_characters) - 1U); index++)
    {
        if (font_characters[index] == character)
        {
            return font5x7[index];
        }
    }

    return font5x7[(sizeof(font5x7) / sizeof(font5x7[0])) - 1U];
}

static void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t colour)
{
    if ((x < TEXT_WIDTH) && (y < TEXT_HEIGHT))
    {
        text_framebuffer[((uint32_t)y * TEXT_WIDTH) + x] = colour;
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
            LCD_DrawPixel((uint16_t)(x + px), (uint16_t)(y + py), colour);
        }
    }
}

static void LCD_Clear(uint16_t colour)
{
    uint32_t pixel;

    for (pixel = 0U; pixel < (TEXT_WIDTH * TEXT_HEIGHT); pixel++)
    {
        text_framebuffer[pixel] = colour;
    }
}

static void LCD_DrawCharacter(uint16_t x,
                              uint16_t y,
                              char character,
                              uint16_t colour,
                              uint8_t scale)
{
    const uint8_t *glyph = LCD_GetGlyph(character);
    uint8_t row;
    uint8_t column;

    for (row = 0U; row < 7U; row++)
    {
        for (column = 0U; column < 5U; column++)
        {
            if ((glyph[row] & (uint8_t)(1U << (4U - column))) != 0U)
            {
                LCD_FillRectangle(
                    (uint16_t)(x + ((uint16_t)column * scale)),
                    (uint16_t)(y + ((uint16_t)row * scale)),
                    scale,
                    scale,
                    colour);
            }
        }
    }
}

static void LCD_DrawString(uint16_t x,
                           uint16_t y,
                           const char *text,
                           uint16_t colour,
                           uint8_t scale)
{
    while ((text != NULL) && (*text != '\0'))
    {
        LCD_DrawCharacter(x, y, *text, colour, scale);
        x = (uint16_t)(x + (6U * scale));
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
    uint32_t width = (uint32_t)strlen(text) * 6U * scale;
    uint16_t x = 0U;

    if (width < TEXT_WIDTH)
    {
        x = (uint16_t)((TEXT_WIDTH - width) / 2U);
    }

    LCD_DrawString(x, y, text, colour, scale);
}

static void LCD_Present(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
    {
        SCB_CleanDCache_by_Addr((uint32_t *)text_framebuffer,
                               (int32_t)sizeof(text_framebuffer));
    }

    __DSB();
}

static void LCD_ConfigureDiagnosticLayer(void)
{
    LTDC_LayerCfgTypeDef layer = {0};

    layer.WindowX0 = TEXT_X;
    layer.WindowX1 = TEXT_X + TEXT_WIDTH;
    layer.WindowY0 = TEXT_Y;
    layer.WindowY1 = TEXT_Y + TEXT_HEIGHT;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.FBStartAdress = (uint32_t)text_framebuffer;
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.ImageWidth = TEXT_WIDTH;
    layer.ImageHeight = TEXT_HEIGHT;
    layer.Backcolor.Red = 0U;
    layer.Backcolor.Green = 0U;
    layer.Backcolor.Blue = 0U;

    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0U) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LTDC_LAYER_ENABLE(&hltdc, 0U);
    __HAL_LTDC_LAYER_DISABLE(&hltdc, 1U);
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);
}

static const char *SDRAM_StageName(SDRAM_TestStage stage)
{
    switch (stage)
    {
        case SDRAM_STAGE_STARTUP:     return "STARTUP";
        case SDRAM_STAGE_DATA_BUS:    return "DATA BUS";
        case SDRAM_STAGE_BYTE_MASK:   return "BYTE MASK";
        case SDRAM_STAGE_WORD32:      return "32 BIT WORD";
        case SDRAM_STAGE_ADDRESS_BUS: return "ADDRESS BUS";
        case SDRAM_STAGE_FULL_MEMORY: return "FULL 8MB";
        case SDRAM_STAGE_PASS:        return "ALL TESTS";
        default:                      return "UNKNOWN";
    }
}

static void LCD_ShowProgress(SDRAM_TestStage stage)
{
    char line[32];

    LCD_Clear(LCD_DARK_BLUE);
    LCD_DrawCenteredString(4U, "SDRAM DIAGNOSTIC", LCD_CYAN, 2U);

    (void)snprintf(line, sizeof(line),
                   "TEST:%06lu CLK:%s",
                   (unsigned long)test_cycle,
                   SDRAM_CLOCK_LABEL);
    LCD_DrawCenteredString(26U, line, LCD_WHITE, 2U);

    LCD_DrawCenteredString(52U, "TESTING", LCD_YELLOW, 2U);
    LCD_DrawCenteredString(76U, SDRAM_StageName(stage), LCD_WHITE, 3U);
    LCD_DrawCenteredString(108U, "PLEASE WAIT", LCD_CYAN, 2U);
    LCD_Present();
}

static void LCD_ShowResult(bool passed, const SDRAM_TestResult *result)
{
    char line[32];

    LCD_Clear(LCD_DARK_BLUE);
    LCD_DrawCenteredString(1U,
                           passed ? "SDRAM PASS" : "SDRAM FAIL",
                           passed ? LCD_GREEN : LCD_RED,
                           2U);

    (void)snprintf(line, sizeof(line),
                   "N:%06lu F:%05lu %s",
                   (unsigned long)test_cycle,
                   (unsigned long)failure_count,
                   SDRAM_CLOCK_LABEL);
    LCD_DrawString(2U, 19U, line, LCD_WHITE, 2U);

    (void)snprintf(line, sizeof(line), "STAGE:%s", SDRAM_StageName(result->stage));
    LCD_DrawString(2U, 37U, line, passed ? LCD_GREEN : LCD_YELLOW, 2U);

    if (passed)
    {
        LCD_DrawCenteredString(65U, "ALL 8MB VERIFIED", LCD_GREEN, 2U);
        LCD_DrawCenteredString(91U, "REPEATING TEST", LCD_CYAN, 2U);
    }
    else
    {
        /* Keep the same compact format the previous firmware used. */
        (void)snprintf(line, sizeof(line), "A:%08lX", (unsigned long)result->address);
        LCD_DrawString(2U, 55U, line, LCD_WHITE, 2U);

        (void)snprintf(line, sizeof(line), "E:%08lX", (unsigned long)result->expected);
        LCD_DrawString(2U, 73U, line, LCD_YELLOW, 2U);

        (void)snprintf(line, sizeof(line), "R:%08lX", (unsigned long)result->actual_first);
        LCD_DrawString(2U, 91U, line, LCD_CYAN, 2U);

        (void)snprintf(line, sizeof(line), "R2:%08lX", (unsigned long)result->actual_second);
        LCD_DrawString(2U, 109U, line,
                       (result->actual_first == result->actual_second) ? LCD_WHITE : LCD_RED,
                       2U);
    }

    LCD_Present();
}

/* -------------------------------------------------------------------------- */
/* UART diagnostic output                                                     */
/* -------------------------------------------------------------------------- */

static void Debug_Printf(const char *format, ...)
{
    char buffer[192];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    if (length <= 0)
    {
        return;
    }

    if ((size_t)length >= sizeof(buffer))
    {
        length = (int)sizeof(buffer) - 1;
    }

    (void)HAL_UART_Transmit(&huart3,
                            (uint8_t *)buffer,
                            (uint16_t)length,
                            1000U);
}

/* -------------------------------------------------------------------------- */
/* SDRAM initialization                                                       */
/* -------------------------------------------------------------------------- */

static HAL_StatusTypeDef SDRAM_SendCommand(uint32_t command_mode,
                                            uint32_t auto_refresh,
                                            uint32_t mode_register)
{
    FMC_SDRAM_CommandTypeDef command = {0};

    command.CommandMode = command_mode;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber = auto_refresh;
    command.ModeRegisterDefinition = mode_register;

    return HAL_SDRAM_SendCommand(&hsdram1, &command, 1000U);
}

static HAL_StatusTypeDef SDRAM_Startup(void)
{
    uint32_t mode_register;

    if (SDRAM_SendCommand(FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* IS42S16400J requires at least 100 us after stable clock/CKE. */
    HAL_Delay(1U);

    if (SDRAM_SendCommand(FMC_SDRAM_CMD_PALL, 1U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (SDRAM_SendCommand(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    mode_register =
        SDRAM_MODEREG_BURST_LENGTH_1 |
        SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
        SDRAM_MODEREG_CAS_LATENCY_3 |
        SDRAM_MODEREG_OPERATING_MODE_STANDARD |
        SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    if (SDRAM_SendCommand(FMC_SDRAM_CMD_LOAD_MODE,
                          1U,
                          mode_register) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_SDRAM_ProgramRefreshRate(&hsdram1, SDRAM_REFRESH_COUNT);
}

/* -------------------------------------------------------------------------- */
/* SDRAM tests                                                                */
/* -------------------------------------------------------------------------- */

static void SDRAM_SetFailure(SDRAM_TestResult *result,
                             SDRAM_TestStage stage,
                             uint32_t address,
                             uint32_t expected,
                             uint32_t actual_first,
                             uint32_t actual_second)
{
    result->stage = stage;
    result->address = address;
    result->expected = expected;
    result->actual_first = actual_first;
    result->actual_second = actual_second;
}

static bool SDRAM_TestDataBus(SDRAM_TestResult *result)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    uint32_t bit;

    for (bit = 0U; bit < 16U; bit++)
    {
        uint16_t pattern = (uint16_t)(1UL << bit);
        uint16_t actual_first;
        uint16_t actual_second;

        memory[0] = pattern;
        __DSB();
        actual_first = memory[0];
        actual_second = memory[0];

        if ((actual_first != pattern) || (actual_second != pattern))
        {
            SDRAM_SetFailure(result,
                             SDRAM_STAGE_DATA_BUS,
                             SDRAM_BASE_ADDRESS,
                             pattern,
                             actual_first,
                             actual_second);
            return false;
        }

        pattern = (uint16_t)(~pattern);
        memory[0] = pattern;
        __DSB();
        actual_first = memory[0];
        actual_second = memory[0];

        if ((actual_first != pattern) || (actual_second != pattern))
        {
            SDRAM_SetFailure(result,
                             SDRAM_STAGE_DATA_BUS,
                             SDRAM_BASE_ADDRESS,
                             pattern,
                             actual_first,
                             actual_second);
            return false;
        }
    }

    return true;
}

/* Tests FMC_NBL0 -> LDQM and FMC_NBL1 -> UDQM independently. */
static bool SDRAM_TestByteMasks(SDRAM_TestResult *result)
{
    volatile uint16_t *word = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    volatile uint8_t *byte = (volatile uint8_t *)SDRAM_BASE_ADDRESS;
    uint16_t actual_first;
    uint16_t actual_second;

    word[0] = 0xA55AU;
    byte[0] = 0x3CU;
    __DSB();
    actual_first = word[0];
    actual_second = word[0];

    if ((actual_first != 0xA53CU) || (actual_second != 0xA53CU))
    {
        SDRAM_SetFailure(result,
                         SDRAM_STAGE_BYTE_MASK,
                         SDRAM_BASE_ADDRESS,
                         0xA53CU,
                         actual_first,
                         actual_second);
        return false;
    }

    byte[1] = 0xC3U;
    __DSB();
    actual_first = word[0];
    actual_second = word[0];

    if ((actual_first != 0xC33CU) || (actual_second != 0xC33CU))
    {
        SDRAM_SetFailure(result,
                         SDRAM_STAGE_BYTE_MASK,
                         SDRAM_BASE_ADDRESS,
                         0xC33CU,
                         actual_first,
                         actual_second);
        return false;
    }

    return true;
}

static bool SDRAM_Test32BitWord(SDRAM_TestResult *result)
{
    volatile uint32_t *memory = (volatile uint32_t *)SDRAM_BASE_ADDRESS;
    static const uint32_t patterns[] =
    {
        0xA5A55A5AUL,
        0x5A5AA5A5UL,
        0x00000000UL,
        0xFFFFFFFFUL
    };
    uint32_t index;

    for (index = 0U; index < (sizeof(patterns) / sizeof(patterns[0])); index++)
    {
        uint32_t actual_first;
        uint32_t actual_second;

        memory[0] = patterns[index];
        __DSB();
        actual_first = memory[0];
        actual_second = memory[0];

        if ((actual_first != patterns[index]) ||
            (actual_second != patterns[index]))
        {
            SDRAM_SetFailure(result,
                             SDRAM_STAGE_WORD32,
                             SDRAM_BASE_ADDRESS,
                             patterns[index],
                             actual_first,
                             actual_second);
            return false;
        }
    }

    return true;
}

static bool SDRAM_TestAddressBus(SDRAM_TestResult *result)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    /*
     * The highest halfword offset that lies inside the 8-MiB device is
     * 0x003FFFFF. Test power-of-two offsets from 1 through 0x00200000.
     */
    const uint32_t address_mask = SDRAM_HALFWORD_COUNT - 1UL;
    const uint16_t pattern = 0xAAAAU;
    const uint16_t antipattern = 0x5555U;
    uint32_t offset;
    uint32_t test_offset;

    /* Put the normal pattern at every power-of-two address. */
    for (offset = 1UL; offset <= address_mask; offset <<= 1U)
    {
        memory[offset] = pattern;
    }

    /* Check for an address line stuck high. */
    memory[0] = antipattern;
    __DSB();

    for (offset = 1UL; offset <= address_mask; offset <<= 1U)
    {
        uint16_t actual_first = memory[offset];
        uint16_t actual_second = memory[offset];

        if ((actual_first != pattern) || (actual_second != pattern))
        {
            SDRAM_SetFailure(result,
                             SDRAM_STAGE_ADDRESS_BUS,
                             SDRAM_BASE_ADDRESS + (offset * 2UL),
                             pattern,
                             actual_first,
                             actual_second);
            return false;
        }
    }

    memory[0] = pattern;

    /* Check for each address line stuck low or shorted to another line. */
    for (test_offset = 1UL;
         test_offset <= address_mask;
         test_offset <<= 1U)
    {
        memory[test_offset] = antipattern;
        __DSB();

        if (memory[0] != pattern)
        {
            uint16_t actual_first = memory[0];
            uint16_t actual_second = memory[0];

            SDRAM_SetFailure(result,
                             SDRAM_STAGE_ADDRESS_BUS,
                             SDRAM_BASE_ADDRESS,
                             pattern,
                             actual_first,
                             actual_second);
            return false;
        }

        for (offset = 1UL; offset <= address_mask; offset <<= 1U)
        {
            if (offset != test_offset)
            {
                uint16_t actual_first = memory[offset];
                uint16_t actual_second = memory[offset];

                if ((actual_first != pattern) || (actual_second != pattern))
                {
                    SDRAM_SetFailure(result,
                                     SDRAM_STAGE_ADDRESS_BUS,
                                     SDRAM_BASE_ADDRESS + (offset * 2UL),
                                     pattern,
                                     actual_first,
                                     actual_second);
                    return false;
                }
            }
        }

        memory[test_offset] = pattern;
    }

    return true;
}

static uint16_t SDRAM_DevicePattern(uint32_t index, bool inverted)
{
    uint16_t pattern = (uint16_t)(
        (index * 0x9E37UL) ^
        (index >> 16U) ^
        0xA55AUL);

    return inverted ? (uint16_t)(~pattern) : pattern;
}

static bool SDRAM_TestFullMemory(SDRAM_TestResult *result)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    uint32_t pass;
    uint32_t index;

    for (pass = 0U; pass < 2U; pass++)
    {
        bool inverted = (pass != 0U);

        for (index = 0U; index < SDRAM_HALFWORD_COUNT; index++)
        {
            memory[index] = SDRAM_DevicePattern(index, inverted);
        }

        __DSB();

        for (index = 0U; index < SDRAM_HALFWORD_COUNT; index++)
        {
            uint16_t expected = SDRAM_DevicePattern(index, inverted);
            uint16_t actual_first = memory[index];

            if (actual_first != expected)
            {
                uint16_t actual_second = memory[index];

                SDRAM_SetFailure(result,
                                 SDRAM_STAGE_FULL_MEMORY,
                                 SDRAM_BASE_ADDRESS + (index * 2UL),
                                 expected,
                                 actual_first,
                                 actual_second);
                return false;
            }
        }
    }

    return true;
}

static bool SDRAM_RunAllTests(SDRAM_TestResult *result)
{
    memset(result, 0, sizeof(*result));

    LCD_ShowProgress(SDRAM_STAGE_DATA_BUS);
    if (!SDRAM_TestDataBus(result))
    {
        return false;
    }

    LCD_ShowProgress(SDRAM_STAGE_BYTE_MASK);
    if (!SDRAM_TestByteMasks(result))
    {
        return false;
    }

    LCD_ShowProgress(SDRAM_STAGE_WORD32);
    if (!SDRAM_Test32BitWord(result))
    {
        return false;
    }

    LCD_ShowProgress(SDRAM_STAGE_ADDRESS_BUS);
    if (!SDRAM_TestAddressBus(result))
    {
        return false;
    }

    LCD_ShowProgress(SDRAM_STAGE_FULL_MEMORY);
    if (!SDRAM_TestFullMemory(result))
    {
        return false;
    }

    result->stage = SDRAM_STAGE_PASS;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    SDRAM_TestResult result;
    bool startup_ok = false;

    MPU_Config();
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_LTDC_Init();
    LCD_ConfigureDiagnosticLayer();
    MX_USART3_UART_Init();
    MX_FMC_Init();

    LCD_Clear(LCD_DARK_BLUE);
    LCD_DrawCenteredString(18U, "SDRAM DIAGNOSTIC", LCD_CYAN, 2U);
    LCD_DrawCenteredString(48U, "FMC CLOCK " SDRAM_CLOCK_LABEL, LCD_WHITE, 2U);
    LCD_DrawCenteredString(76U, "STARTING SDRAM", LCD_YELLOW, 2U);
    LCD_Present();

    Debug_Printf("\r\nSTM32F746 SDRAM DIAGNOSTIC\r\n");
    Debug_Printf("HCLK=200MHz FMC=%s refresh=%u\r\n",
                 SDRAM_CLOCK_LABEL,
                 (unsigned int)SDRAM_REFRESH_COUNT);

    while (!startup_ok)
    {
        startup_ok = (SDRAM_Startup() == HAL_OK);

        if (!startup_ok)
        {
            memset(&result, 0, sizeof(result));
            result.stage = SDRAM_STAGE_STARTUP;
            result.address = SDRAM_BASE_ADDRESS;
            failure_count++;
            LCD_ShowResult(false, &result);
            Debug_Printf("STARTUP FAIL: HAL command/refresh error\r\n");
            HAL_Delay(1500U);
        }
    }

    Debug_Printf("SDRAM command initialization returned HAL_OK\r\n");

    while (1)
    {
        bool passed;

        test_cycle++;
        passed = SDRAM_RunAllTests(&result);

        if (!passed)
        {
            failure_count++;
        }

        LCD_ShowResult(passed, &result);

        if (passed)
        {
            Debug_Printf(
                "PASS cycle=%lu: data, masks, address and all 8MB verified\r\n",
                (unsigned long)test_cycle);
        }
        else
        {
            Debug_Printf(
                "FAIL cycle=%lu stage=%s A=%08lX E=%08lX "
                "R1=%08lX R2=%08lX XOR=%08lX\r\n",
                (unsigned long)test_cycle,
                SDRAM_StageName(result.stage),
                (unsigned long)result.address,
                (unsigned long)result.expected,
                (unsigned long)result.actual_first,
                (unsigned long)result.actual_second,
                (unsigned long)(result.expected ^ result.actual_first));
        }

        HAL_Delay(passed ? 800U : 1500U);
    }
}

/* -------------------------------------------------------------------------- */
/* Clock configuration                                                        */
/* -------------------------------------------------------------------------- */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /*
     * The PCB uses an ASCO-8.000MHz powered oscillator driving OSC_IN.
     * It is NOT a two-pin passive crystal, therefore HSE bypass is required.
     */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_BYPASS;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 4U;
    oscillator.PLL.PLLN = 200U;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 2U;

    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_PWREx_EnableOverDrive() != HAL_OK)
    {
        Error_Handler();
    }

    clocks.ClockType = RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 |
                       RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV4;
    clocks.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_6) != HAL_OK)
    {
        Error_Handler();
    }
}

/* -------------------------------------------------------------------------- */
/* Peripheral initialization                                                  */
/* -------------------------------------------------------------------------- */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port,
                      LCD_BL_CTRL_Pin,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DISP_GPIO_Port,
                      LCD_DISP_Pin,
                      GPIO_PIN_RESET);

    gpio.Pin = LCD_BL_CTRL_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_BL_CTRL_GPIO_Port, &gpio);

    gpio.Pin = LCD_DISP_Pin;
    HAL_GPIO_Init(LCD_DISP_GPIO_Port, &gpio);

    HAL_GPIO_WritePin(LCD_DISP_GPIO_Port,
                      LCD_DISP_Pin,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port,
                      LCD_BL_CTRL_Pin,
                      GPIO_PIN_SET);
    HAL_Delay(20U);
}

static void MX_LTDC_Init(void)
{
    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    hltdc.Init.HorizontalSync = 7U;
    hltdc.Init.VerticalSync = 3U;
    hltdc.Init.AccumulatedHBP = 14U;
    hltdc.Init.AccumulatedVBP = 5U;
    hltdc.Init.AccumulatedActiveW = 494U;
    hltdc.Init.AccumulatedActiveH = 277U;
    hltdc.Init.TotalWidth = 500U;
    hltdc.Init.TotalHeigh = 279U;
    hltdc.Init.Backcolor.Blue = 0U;
    hltdc.Init.Backcolor.Green = 0U;
    hltdc.Init.Backcolor.Red = 0U;

    if (HAL_LTDC_Init(&hltdc) != HAL_OK)
    {
        Error_Handler();
    }

    LTDC->BCCR = 0x00000412UL;
    LTDC->SRCR = LTDC_SRCR_IMR;
}

static void MX_USART3_UART_Init(void)
{
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 115200U;
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
}

static void MX_FMC_Init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {0};

    hsdram1.Instance = FMC_SDRAM_DEVICE;
    hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
    hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
    hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram1.Init.SDClockPeriod = SDRAM_CLOCK_PERIOD;
    hsdram1.Init.ReadBurst = SDRAM_READ_BURST;
    hsdram1.Init.ReadPipeDelay = SDRAM_READ_PIPE;

    /* Conservative values, valid for the 66.667-MHz diagnostic clock. */
    timing.LoadToActiveDelay = 2U;
    timing.ExitSelfRefreshDelay = 8U;
    timing.SelfRefreshTime = 5U;
    timing.RowCycleDelay = 7U;
    timing.WriteRecoveryTime = 3U;
    timing.RPDelay = 2U;
    timing.RCDDelay = 2U;

    if (HAL_SDRAM_Init(&hsdram1, &timing) != HAL_OK)
    {
        Error_Handler();
    }
}

/* -------------------------------------------------------------------------- */
/* MPU: make SDRAM normal, non-cacheable memory during diagnostics             */
/* -------------------------------------------------------------------------- */

static void MPU_Config(void)
{
    MPU_Region_InitTypeDef region = {0};

    HAL_MPU_Disable();

    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = SDRAM_BASE_ADDRESS;
    region.Size = MPU_REGION_SIZE_8MB;
    region.SubRegionDisable = 0x00U;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
        /* A debugger breakpoint here identifies a HAL initialization error. */
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
