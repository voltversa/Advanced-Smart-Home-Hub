#include "qspi_assets.h"
#include <stddef.h>
#include <string.h>

#if QSPI_ASSET_INSTALLER
#include "weather_backgrounds.h"
#include "gui_modern_assets.h"

typedef char QSPI_ModernAssetSize_must_match[
    (GUI_MODERN_ASSET_BYTES == QSPI_ASSET_MODERN_BYTES) ? 1 : -1
];
typedef char QSPI_WeatherCount_must_match[
    (WEATHER_BACKGROUND_COUNT == QSPI_WEATHER_BACKGROUND_COUNT) ? 1 : -1
];
typedef char QSPI_WeatherDimensions_must_be_half_size[
    ((WEATHER_BACKGROUND_LOW_WIDTH * 2U == 480U) &&
     (WEATHER_BACKGROUND_LOW_HEIGHT * 2U == 272U)) ? 1 : -1
];
#endif

/* W25Q128 commands used by this asset store. */
#define W25Q_CMD_RESET_ENABLE       0x66U
#define W25Q_CMD_RESET              0x99U
#define W25Q_CMD_JEDEC_ID           0x9FU
#define W25Q_CMD_WRITE_ENABLE       0x06U
#define W25Q_CMD_READ_SR1           0x05U
#define W25Q_CMD_READ_SR2           0x35U
#define W25Q_CMD_WRITE_SR2          0x31U
#define W25Q_CMD_SECTOR_ERASE       0x20U
#define W25Q_CMD_QUAD_PAGE_PROGRAM  0x32U
#define W25Q_CMD_QUAD_READ          0x6BU

#define W25Q_PAGE_BYTES             256UL
#define W25Q_SECTOR_BYTES           4096UL
#define W25Q_ASSET_REGION_END       0x00104000UL
#define W25Q_EXPECTED_ID0           0xEFU
#define W25Q_EXPECTED_ID1           0x40U
#define W25Q_EXPECTED_ID2           0x18U

#define FONT_GLYPH_WIDTH            5UL
#define FONT_GLYPH_HEIGHT           7UL
#define FONT_CHARACTER_COUNT        43UL
#define FONT_MAP_BYTES              (FONT_CHARACTER_COUNT + 1UL)
#define FONT_GLYPH_BYTES            \
    (FONT_CHARACTER_COUNT * FONT_GLYPH_HEIGHT)
#define QSPI_ASSET_FONT_GLYPH_OFFSET \
    (QSPI_ASSET_FONT_MAP_OFFSET + FONT_MAP_BYTES)
#define QSPI_ASSET_FONT_TOTAL_BYTES \
    (FONT_MAP_BYTES + FONT_GLYPH_BYTES)

#define MODERN_ASSET_MAGIC          0x544E464DUL /* "MFNT" */
#define MODERN_ASSET_VERSION        1U
#define MODERN_HEADER_BYTES         64UL
#define MODERN_DESCRIPTOR_BYTES     12UL
#define MODERN_FIRST_CHARACTER      32U
#define MODERN_LAST_CHARACTER       90U
#define MODERN_GLYPH_COUNT          \
    (MODERN_LAST_CHARACTER - MODERN_FIRST_CHARACTER + 1U)
#define MODERN_ICON_ROW_BYTES       \
    ((QSPI_MODERN_ICON_WIDTH + 7U) / 8U)

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t background_offset;
    uint32_t background_size;
    uint32_t background_crc32;
    uint32_t clut_offset;
    uint32_t clut_size;
    uint32_t clut_crc32;
    uint32_t font_map_offset;
    uint32_t font_map_size;
    uint32_t font_glyph_offset;
    uint32_t font_glyph_size;
    uint32_t font_crc32;
    uint32_t glyph_count;
    uint32_t modern_offset;
    uint32_t modern_size;
    uint32_t modern_crc32;
    uint32_t header_crc32;
} QSPI_AssetHeader;

typedef char QSPI_AssetHeader_must_be_72_bytes[
    (sizeof(QSPI_AssetHeader) == 72U) ? 1 : -1
];

static QSPI_HandleTypeDef *asset_qspi;
static QSPI_AssetError asset_error = QSPI_ASSET_ERROR_NONE;
static bool asset_memory_mapped;

#if QSPI_ASSET_INSTALLER

static const char source_font_characters[FONT_MAP_BYTES] =
    " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!-.:%?";

static const uint8_t source_font5x7[FONT_CHARACTER_COUNT][7] =
{
    /* Space */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},

    /* 0-9 */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},

    /* A-Z */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},

    /* ! - . : % ? */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00},
    {0x19,0x1A,0x04,0x08,0x16,0x13,0x00},
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}
};

#endif /* QSPI_ASSET_INSTALLER */

static void QSPI_CommandDefaults(QSPI_CommandTypeDef *command,
                                 uint8_t instruction)
{
    memset(command, 0, sizeof(*command));
    command->Instruction = instruction;
    command->InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command->AddressMode = QSPI_ADDRESS_NONE;
    command->AddressSize = QSPI_ADDRESS_24_BITS;
    command->AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command->DataMode = QSPI_DATA_NONE;
    command->DummyCycles = 0U;
    command->DdrMode = QSPI_DDR_MODE_DISABLE;
    command->DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command->SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
}

static HAL_StatusTypeDef QSPI_CommandOnly(uint8_t instruction)
{
    QSPI_CommandTypeDef command;

    QSPI_CommandDefaults(&command, instruction);
    return HAL_QSPI_Command(asset_qspi, &command, 1000U);
}

static HAL_StatusTypeDef QSPI_ReadRegister(uint8_t instruction,
                                           uint8_t *value)
{
    QSPI_CommandTypeDef command;

    QSPI_CommandDefaults(&command, instruction);
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;

    if (HAL_QSPI_Command(asset_qspi, &command, 1000U) != HAL_OK)
        return HAL_ERROR;

    return HAL_QSPI_Receive(asset_qspi, value, 1000U);
}

static HAL_StatusTypeDef QSPI_PollStatus(uint8_t match,
                                         uint8_t mask,
                                         uint32_t timeout)
{
    QSPI_CommandTypeDef command;
    QSPI_AutoPollingTypeDef polling;

    QSPI_CommandDefaults(&command, W25Q_CMD_READ_SR1);
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;

    memset(&polling, 0, sizeof(polling));
    polling.Match = match;
    polling.Mask = mask;
    polling.MatchMode = QSPI_MATCH_MODE_AND;
    polling.StatusBytesSize = 1U;
    polling.Interval = 0x10U;
    polling.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;

    return HAL_QSPI_AutoPolling(asset_qspi,
                                &command,
                                &polling,
                                timeout);
}

static HAL_StatusTypeDef QSPI_WaitReady(uint32_t timeout)
{
    return QSPI_PollStatus(0x00U, 0x01U, timeout);
}

static HAL_StatusTypeDef QSPI_WriteEnable(void)
{
    if (QSPI_CommandOnly(W25Q_CMD_WRITE_ENABLE) != HAL_OK)
        return HAL_ERROR;

    return QSPI_PollStatus(0x02U, 0x02U, 1000U);
}

static HAL_StatusTypeDef QSPI_EnableQuadMode(void)
{
    QSPI_CommandTypeDef command;
    uint8_t status2;

    if (QSPI_ReadRegister(W25Q_CMD_READ_SR2, &status2) != HAL_OK)
        return HAL_ERROR;

    if ((status2 & 0x02U) != 0U)
        return HAL_OK;

    status2 |= 0x02U;

    if (QSPI_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    QSPI_CommandDefaults(&command, W25Q_CMD_WRITE_SR2);
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = 1U;

    if (HAL_QSPI_Command(asset_qspi, &command, 1000U) != HAL_OK)
        return HAL_ERROR;

    if (HAL_QSPI_Transmit(asset_qspi, &status2, 1000U) != HAL_OK)
        return HAL_ERROR;

    if (QSPI_WaitReady(1000U) != HAL_OK)
        return HAL_ERROR;

    if (QSPI_ReadRegister(W25Q_CMD_READ_SR2, &status2) != HAL_OK)
        return HAL_ERROR;

    return ((status2 & 0x02U) != 0U) ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef QSPI_Read(uint32_t address,
                                   uint8_t *destination,
                                   uint32_t length)
{
    QSPI_CommandTypeDef command;

    if ((destination == NULL) || (length == 0U))
        return HAL_ERROR;

    QSPI_CommandDefaults(&command, W25Q_CMD_QUAD_READ);
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.Address = address;
    command.DataMode = QSPI_DATA_4_LINES;
    command.DummyCycles = 8U;
    command.NbData = length;

    if (HAL_QSPI_Command(asset_qspi, &command, 1000U) != HAL_OK)
        return HAL_ERROR;

    return HAL_QSPI_Receive(asset_qspi, destination, 5000U);
}

#if QSPI_ASSET_INSTALLER

static HAL_StatusTypeDef QSPI_EraseAssetRegion(void)
{
    QSPI_CommandTypeDef command;

    for (uint32_t address = 0U;
         address < W25Q_ASSET_REGION_END;
         address += W25Q_SECTOR_BYTES)
    {
        if (QSPI_WriteEnable() != HAL_OK)
            return HAL_ERROR;

        QSPI_CommandDefaults(&command, W25Q_CMD_SECTOR_ERASE);
        command.AddressMode = QSPI_ADDRESS_1_LINE;
        command.Address = address;

        if (HAL_QSPI_Command(asset_qspi, &command, 1000U) != HAL_OK)
            return HAL_ERROR;

        if (QSPI_WaitReady(5000U) != HAL_OK)
            return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef QSPI_Program(uint32_t address,
                                      const uint8_t *source,
                                      uint32_t length)
{
    QSPI_CommandTypeDef command;

    if ((source == NULL) || (length == 0U))
        return HAL_ERROR;

    while (length != 0U)
    {
        uint32_t page_remaining =
            W25Q_PAGE_BYTES - (address & (W25Q_PAGE_BYTES - 1UL));
        uint32_t chunk = (length < page_remaining) ?
                         length : page_remaining;

        if (QSPI_WriteEnable() != HAL_OK)
            return HAL_ERROR;

        QSPI_CommandDefaults(&command, W25Q_CMD_QUAD_PAGE_PROGRAM);
        command.AddressMode = QSPI_ADDRESS_1_LINE;
        command.Address = address;
        command.DataMode = QSPI_DATA_4_LINES;
        command.NbData = chunk;

        if (HAL_QSPI_Command(asset_qspi, &command, 1000U) != HAL_OK)
            return HAL_ERROR;

        if (HAL_QSPI_Transmit(asset_qspi,
                              (uint8_t *)(uintptr_t)source,
                              1000U) != HAL_OK)
        {
            return HAL_ERROR;
        }

        if (QSPI_WaitReady(1000U) != HAL_OK)
            return HAL_ERROR;

        address += chunk;
        source += chunk;
        length -= chunk;
    }

    return HAL_OK;
}

#endif /* QSPI_ASSET_INSTALLER */

static uint32_t CRC32_Update(uint32_t crc,
                             const uint8_t *data,
                             uint32_t length)
{
    while (length-- != 0U)
    {
        crc ^= *data++;

        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return crc;
}

static uint32_t CRC32_Buffer(const uint8_t *data, uint32_t length)
{
    return CRC32_Update(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
}

#if QSPI_ASSET_INSTALLER

static uint32_t QSPI_SourceFontCRC(void)
{
    uint32_t crc = 0xFFFFFFFFUL;

    crc = CRC32_Update(crc,
                       (const uint8_t *)source_font_characters,
                       FONT_MAP_BYTES);
    crc = CRC32_Update(crc,
                       (const uint8_t *)source_font5x7,
                       FONT_GLYPH_BYTES);

    return crc ^ 0xFFFFFFFFUL;
}

/* The installer keeps eight 240x136 pictures in MCU flash so it still fits
 * in the STM32F746.  Each source pixel is expanded to a 2x2 block while it
 * is written, producing native 480x272 L8 frames in QSPI. */
static uint32_t QSPI_SourceWeatherCRC(void)
{
    uint8_t expanded_row[480U];
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t theme = 0U;
         theme < WEATHER_BACKGROUND_COUNT;
         theme++)
    {
        for (uint32_t y = 0U;
             y < WEATHER_BACKGROUND_LOW_HEIGHT;
             y++)
        {
            const uint8_t *source_row =
                &weather_backgrounds_l8[theme]
                                       [y * WEATHER_BACKGROUND_LOW_WIDTH];

            for (uint32_t x = 0U;
                 x < WEATHER_BACKGROUND_LOW_WIDTH;
                 x++)
            {
                expanded_row[(x * 2U) + 0U] = source_row[x];
                expanded_row[(x * 2U) + 1U] = source_row[x];
            }

            crc = CRC32_Update(crc, expanded_row, sizeof(expanded_row));
            crc = CRC32_Update(crc, expanded_row, sizeof(expanded_row));
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static bool QSPI_HeaderMatchesInstallerSource(
    const QSPI_AssetHeader *header)
{
    return (header->background_crc32 == QSPI_SourceWeatherCRC()) &&
           (header->clut_crc32 ==
            CRC32_Buffer((const uint8_t *)weather_background_clut,
                         QSPI_ASSET_CLUT_BYTES)) &&
           (header->font_crc32 == QSPI_SourceFontCRC()) &&
           (header->modern_crc32 ==
            CRC32_Buffer((const uint8_t *)gui_modern_assets,
                         QSPI_ASSET_MODERN_BYTES));
}

#endif

static HAL_StatusTypeDef QSPI_CRCRegion(uint32_t address,
                                        uint32_t length,
                                        uint32_t *result)
{
    uint8_t buffer[W25Q_PAGE_BYTES];
    uint32_t crc = 0xFFFFFFFFUL;

    if (result == NULL)
        return HAL_ERROR;

    while (length != 0U)
    {
        uint32_t chunk = (length < sizeof(buffer)) ?
                         length : (uint32_t)sizeof(buffer);

        if (QSPI_Read(address, buffer, chunk) != HAL_OK)
            return HAL_ERROR;

        crc = CRC32_Update(crc, buffer, chunk);
        address += chunk;
        length -= chunk;
    }

    *result = crc ^ 0xFFFFFFFFUL;
    return HAL_OK;
}

static bool QSPI_HeaderIsValid(const QSPI_AssetHeader *header)
{
    uint32_t expected_header_crc;

    if (header == NULL)
        return false;

    if ((header->magic != QSPI_ASSET_MAGIC) ||
        (header->version != QSPI_ASSET_VERSION) ||
        (header->background_offset != QSPI_ASSET_BACKGROUND_OFFSET) ||
        (header->background_size != QSPI_ASSET_BACKGROUND_BYTES) ||
        (header->clut_offset != QSPI_ASSET_CLUT_OFFSET) ||
        (header->clut_size != QSPI_ASSET_CLUT_BYTES) ||
        (header->font_map_offset != QSPI_ASSET_FONT_MAP_OFFSET) ||
        (header->font_map_size != FONT_MAP_BYTES) ||
        (header->font_glyph_offset != QSPI_ASSET_FONT_GLYPH_OFFSET) ||
        (header->font_glyph_size != FONT_GLYPH_BYTES) ||
        (header->glyph_count != FONT_CHARACTER_COUNT) ||
        (header->modern_offset != QSPI_ASSET_MODERN_OFFSET) ||
        (header->modern_size != QSPI_ASSET_MODERN_BYTES))
    {
        return false;
    }

    expected_header_crc = CRC32_Buffer(
        (const uint8_t *)header,
        (uint32_t)offsetof(QSPI_AssetHeader, header_crc32)
    );

    return expected_header_crc == header->header_crc32;
}

static HAL_StatusTypeDef QSPI_VerifyAssets(const QSPI_AssetHeader *header)
{
    uint32_t crc;

    if (!QSPI_HeaderIsValid(header))
        return HAL_ERROR;

    if (QSPI_CRCRegion(header->background_offset,
                       header->background_size,
                       &crc) != HAL_OK ||
        crc != header->background_crc32)
    {
        return HAL_ERROR;
    }

    if (QSPI_CRCRegion(header->clut_offset,
                       header->clut_size,
                       &crc) != HAL_OK ||
        crc != header->clut_crc32)
    {
        return HAL_ERROR;
    }

    if (QSPI_CRCRegion(header->font_map_offset,
                       header->font_map_size + header->font_glyph_size,
                       &crc) != HAL_OK ||
        crc != header->font_crc32)
    {
        return HAL_ERROR;
    }

    if (QSPI_CRCRegion(header->modern_offset,
                       header->modern_size,
                       &crc) != HAL_OK ||
        crc != header->modern_crc32)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

#if QSPI_ASSET_INSTALLER

static HAL_StatusTypeDef QSPI_InstallAssets(QSPI_AssetHeader *header)
{
    uint8_t expanded_row[480U];

    if (QSPI_EraseAssetRegion() != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_ERASE;
        return HAL_ERROR;
    }

    for (uint32_t theme = 0U;
         theme < WEATHER_BACKGROUND_COUNT;
         theme++)
    {
        for (uint32_t y = 0U;
             y < WEATHER_BACKGROUND_LOW_HEIGHT;
             y++)
        {
            const uint8_t *source_row =
                &weather_backgrounds_l8[theme]
                                       [y * WEATHER_BACKGROUND_LOW_WIDTH];
            uint32_t destination =
                QSPI_ASSET_BACKGROUND_OFFSET +
                (theme * QSPI_ASSET_BACKGROUND_FRAME_BYTES) +
                ((y * 2U) * 480U);

            for (uint32_t x = 0U;
                 x < WEATHER_BACKGROUND_LOW_WIDTH;
                 x++)
            {
                expanded_row[(x * 2U) + 0U] = source_row[x];
                expanded_row[(x * 2U) + 1U] = source_row[x];
            }

            if ((QSPI_Program(destination,
                              expanded_row,
                              sizeof(expanded_row)) != HAL_OK) ||
                (QSPI_Program(destination + 480U,
                              expanded_row,
                              sizeof(expanded_row)) != HAL_OK))
            {
                asset_error = QSPI_ASSET_ERROR_PROGRAM;
                return HAL_ERROR;
            }
        }
    }

    if (QSPI_Program(QSPI_ASSET_CLUT_OFFSET,
                     (const uint8_t *)weather_background_clut,
                     QSPI_ASSET_CLUT_BYTES) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_PROGRAM;
        return HAL_ERROR;
    }

    if (QSPI_Program(QSPI_ASSET_FONT_MAP_OFFSET,
                     (const uint8_t *)source_font_characters,
                     FONT_MAP_BYTES) != HAL_OK ||
        QSPI_Program(QSPI_ASSET_FONT_GLYPH_OFFSET,
                     (const uint8_t *)source_font5x7,
                     FONT_GLYPH_BYTES) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_PROGRAM;
        return HAL_ERROR;
    }

    if (QSPI_Program(QSPI_ASSET_MODERN_OFFSET,
                     (const uint8_t *)gui_modern_assets,
                     QSPI_ASSET_MODERN_BYTES) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_PROGRAM;
        return HAL_ERROR;
    }

    memset(header, 0xFF, sizeof(*header));
    header->magic = QSPI_ASSET_MAGIC;
    header->version = QSPI_ASSET_VERSION;
    header->background_offset = QSPI_ASSET_BACKGROUND_OFFSET;
    header->background_size = QSPI_ASSET_BACKGROUND_BYTES;
    header->background_crc32 = QSPI_SourceWeatherCRC();
    header->clut_offset = QSPI_ASSET_CLUT_OFFSET;
    header->clut_size = QSPI_ASSET_CLUT_BYTES;
    header->clut_crc32 = CRC32_Buffer(
        (const uint8_t *)weather_background_clut,
        QSPI_ASSET_CLUT_BYTES
    );
    header->font_map_offset = QSPI_ASSET_FONT_MAP_OFFSET;
    header->font_map_size = FONT_MAP_BYTES;
    header->font_glyph_offset = QSPI_ASSET_FONT_GLYPH_OFFSET;
    header->font_glyph_size = FONT_GLYPH_BYTES;

    header->font_crc32 = QSPI_SourceFontCRC();
    header->glyph_count = FONT_CHARACTER_COUNT;
    header->modern_offset = QSPI_ASSET_MODERN_OFFSET;
    header->modern_size = QSPI_ASSET_MODERN_BYTES;
    header->modern_crc32 = CRC32_Buffer(
        (const uint8_t *)gui_modern_assets,
        QSPI_ASSET_MODERN_BYTES
    );
    header->header_crc32 = CRC32_Buffer(
        (const uint8_t *)header,
        (uint32_t)offsetof(QSPI_AssetHeader, header_crc32)
    );

    /* Header is committed last, so interrupted installs never look valid. */
    if (QSPI_Program(QSPI_ASSET_HEADER_OFFSET,
                     (const uint8_t *)header,
                     sizeof(*header)) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_PROGRAM;
        return HAL_ERROR;
    }

    if (QSPI_Read(QSPI_ASSET_HEADER_OFFSET,
                  (uint8_t *)header,
                  sizeof(*header)) != HAL_OK ||
        QSPI_VerifyAssets(header) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_VERIFY;
        return HAL_ERROR;
    }

    return HAL_OK;
}

#endif /* QSPI_ASSET_INSTALLER */

static HAL_StatusTypeDef QSPI_EnterMemoryMappedMode(void)
{
    QSPI_CommandTypeDef command;
    QSPI_MemoryMappedTypeDef mapped;

    QSPI_CommandDefaults(&command, W25Q_CMD_QUAD_READ);
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.DataMode = QSPI_DATA_4_LINES;
    command.DummyCycles = 8U;

    memset(&mapped, 0, sizeof(mapped));
    mapped.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;

    return HAL_QSPI_MemoryMapped(asset_qspi, &command, &mapped);
}

HAL_StatusTypeDef QSPI_Assets_Init(QSPI_HandleTypeDef *hqspi,
                                   bool *installed_now)
{
    QSPI_CommandTypeDef command;
    QSPI_AssetHeader header;
    uint8_t jedec_id[3];

    asset_qspi = hqspi;
    asset_error = QSPI_ASSET_ERROR_NONE;
    asset_memory_mapped = false;

    if (installed_now != NULL)
        *installed_now = false;

    if ((asset_qspi == NULL) || (asset_qspi->Instance == NULL))
    {
        asset_error = QSPI_ASSET_ERROR_ARGUMENT;
        return HAL_ERROR;
    }

    if (QSPI_CommandOnly(W25Q_CMD_RESET_ENABLE) != HAL_OK ||
        QSPI_CommandOnly(W25Q_CMD_RESET) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_RESET;
        return HAL_ERROR;
    }

    HAL_Delay(1U);

    QSPI_CommandDefaults(&command, W25Q_CMD_JEDEC_ID);
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(jedec_id);

    if (HAL_QSPI_Command(asset_qspi, &command, 1000U) != HAL_OK ||
        HAL_QSPI_Receive(asset_qspi, jedec_id, 1000U) != HAL_OK ||
        jedec_id[0] != W25Q_EXPECTED_ID0 ||
        jedec_id[1] != W25Q_EXPECTED_ID1 ||
        jedec_id[2] != W25Q_EXPECTED_ID2)
    {
        asset_error = QSPI_ASSET_ERROR_JEDEC_ID;
        return HAL_ERROR;
    }

    if (QSPI_EnableQuadMode() != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_QUAD_ENABLE;
        return HAL_ERROR;
    }

    if (QSPI_Read(QSPI_ASSET_HEADER_OFFSET,
                  (uint8_t *)&header,
                  sizeof(header)) != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_HEADER_READ;
        return HAL_ERROR;
    }

    /* Validate the complete stored image before allowing LTDC to read it. */
    bool assets_valid = QSPI_HeaderIsValid(&header) &&
                        (QSPI_VerifyAssets(&header) == HAL_OK);

#if QSPI_ASSET_INSTALLER
    /* Reinstall automatically when the compiled picture or font changed. */
    if (assets_valid && !QSPI_HeaderMatchesInstallerSource(&header))
        assets_valid = false;
#endif

    if (!assets_valid)
    {
#if QSPI_ASSET_INSTALLER
        if (QSPI_InstallAssets(&header) != HAL_OK)
            return HAL_ERROR;

        if (installed_now != NULL)
            *installed_now = true;
#else
        asset_error = QSPI_ASSET_ERROR_NOT_INSTALLED;
        return HAL_ERROR;
#endif
    }

    if (QSPI_EnterMemoryMappedMode() != HAL_OK)
    {
        asset_error = QSPI_ASSET_ERROR_MEMORY_MAP;
        return HAL_ERROR;
    }

    asset_memory_mapped = true;
    return HAL_OK;
}

const uint8_t *QSPI_Assets_BackgroundL8(void)
{
    return QSPI_Assets_WeatherBackgroundL8(QSPI_WEATHER_CLEAR_DAY);
}

const uint8_t *QSPI_Assets_WeatherBackgroundL8(QSPI_WeatherTheme theme)
{
    if (!asset_memory_mapped)
        return NULL;

    if ((uint32_t)theme >= QSPI_WEATHER_BACKGROUND_COUNT)
        theme = QSPI_WEATHER_CLEAR_DAY;

    return (const uint8_t *)(uintptr_t)(
        QSPI_MEMORY_MAPPED_BASE +
        QSPI_ASSET_BACKGROUND_OFFSET +
        ((uint32_t)theme * QSPI_ASSET_BACKGROUND_FRAME_BYTES)
    );
}

const uint32_t *QSPI_Assets_CLUT(void)
{
    if (!asset_memory_mapped)
        return NULL;

    return (const uint32_t *)(uintptr_t)(
        QSPI_MEMORY_MAPPED_BASE + QSPI_ASSET_CLUT_OFFSET
    );
}

const uint8_t *QSPI_Assets_GetGlyph5x7(char character)
{
    const char *character_map;
    const uint8_t *glyphs;
    uint32_t index;

    if (!asset_memory_mapped)
        return NULL;

    if ((character >= 'a') && (character <= 'z'))
        character = (char)(character - ('a' - 'A'));

    character_map = (const char *)(uintptr_t)(
        QSPI_MEMORY_MAPPED_BASE + QSPI_ASSET_FONT_MAP_OFFSET
    );
    glyphs = (const uint8_t *)(uintptr_t)(
        QSPI_MEMORY_MAPPED_BASE + QSPI_ASSET_FONT_GLYPH_OFFSET
    );

    for (index = 0U; index < FONT_CHARACTER_COUNT; index++)
    {
        if (character_map[index] == character)
            return &glyphs[index * FONT_GLYPH_HEIGHT];
    }

    /* The question-mark glyph is the final glyph. */
    return &glyphs[(FONT_CHARACTER_COUNT - 1U) * FONT_GLYPH_HEIGHT];
}

static uint16_t QSPI_ReadLE16(const uint8_t *data)
{
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8U);
}

static uint32_t QSPI_ReadLE32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static const uint8_t *QSPI_ModernAssetBase(void)
{
    const uint8_t *base;

    if (!asset_memory_mapped)
        return NULL;

    base = (const uint8_t *)(uintptr_t)(
        QSPI_MEMORY_MAPPED_BASE + QSPI_ASSET_MODERN_OFFSET
    );

    if ((QSPI_ReadLE32(&base[0]) != MODERN_ASSET_MAGIC) ||
        (QSPI_ReadLE16(&base[4]) != MODERN_ASSET_VERSION) ||
        (QSPI_ReadLE16(&base[6]) != MODERN_GLYPH_COUNT) ||
        (base[8] != MODERN_FIRST_CHARACTER) ||
        (base[9] != MODERN_LAST_CHARACTER) ||
        (base[10] != QSPI_FONT_COUNT) ||
        (base[11] != QSPI_ICON_COUNT) ||
        (base[12] != QSPI_MODERN_ICON_WIDTH) ||
        (base[13] != QSPI_MODERN_ICON_HEIGHT) ||
        (QSPI_ReadLE32(&base[52]) != QSPI_ASSET_MODERN_BYTES))
    {
        return NULL;
    }

    return base;
}

bool QSPI_Assets_GetModernGlyph(QSPI_FontSize size,
                                char character,
                                QSPI_FontGlyph *glyph)
{
    const uint8_t *base = QSPI_ModernAssetBase();
    const uint8_t *descriptor;
    uint32_t descriptor_offset;
    uint32_t bitmap_offset;
    uint32_t data_offset;
    uint32_t data_end;
    uint32_t index;

    if ((base == NULL) || (glyph == NULL) || (size >= QSPI_FONT_COUNT))
        return false;

    if ((character >= 'a') && (character <= 'z'))
        character = (char)(character - ('a' - 'A'));

    if (((uint8_t)character < MODERN_FIRST_CHARACTER) ||
        ((uint8_t)character > MODERN_LAST_CHARACTER))
    {
        character = '?';
    }

    index = (uint32_t)((uint8_t)character - MODERN_FIRST_CHARACTER);
    descriptor_offset = QSPI_ReadLE32(&base[24U + ((uint32_t)size * 4U)]);
    bitmap_offset = QSPI_ReadLE32(&base[36U + ((uint32_t)size * 4U)]);

    if ((descriptor_offset >= QSPI_ASSET_MODERN_BYTES) ||
        (bitmap_offset >= QSPI_ASSET_MODERN_BYTES))
    {
        return false;
    }

    descriptor = &base[descriptor_offset +
                       (index * MODERN_DESCRIPTOR_BYTES)];
    data_offset = QSPI_ReadLE32(&descriptor[8]);
    data_end = bitmap_offset + data_offset +
               ((uint32_t)descriptor[5] * descriptor[3]);

    if (data_end > QSPI_ASSET_MODERN_BYTES)
        return false;

    glyph->left = (int8_t)descriptor[0];
    glyph->top = descriptor[1];
    glyph->width = descriptor[2];
    glyph->height = descriptor[3];
    glyph->advance = descriptor[4];
    glyph->row_bytes = descriptor[5];
    glyph->bitmap = &base[bitmap_offset + data_offset];
    return true;
}

uint8_t QSPI_Assets_ModernLineHeight(QSPI_FontSize size)
{
    const uint8_t *base = QSPI_ModernAssetBase();

    if ((base == NULL) || (size >= QSPI_FONT_COUNT))
        return 0U;

    return base[16U + (uint32_t)size];
}

const uint8_t *QSPI_Assets_GetIcon(QSPI_Icon icon)
{
    const uint8_t *base = QSPI_ModernAssetBase();
    uint32_t icons_offset;
    uint32_t bytes_per_icon =
        MODERN_ICON_ROW_BYTES * QSPI_MODERN_ICON_HEIGHT;
    uint32_t icon_offset;

    if ((base == NULL) || (icon >= QSPI_ICON_COUNT))
        return NULL;

    icons_offset = QSPI_ReadLE32(&base[48]);
    icon_offset = icons_offset + ((uint32_t)icon * bytes_per_icon);

    if ((icon_offset + bytes_per_icon) > QSPI_ASSET_MODERN_BYTES)
        return NULL;

    return &base[icon_offset];
}

QSPI_AssetError QSPI_Assets_LastError(void)
{
    return asset_error;
}
