#include "mcp2221a_console.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MCP2221A_LINE_CAPACITY       96U
#define MCP2221A_PRINTF_CAPACITY     512U
#define MCP2221A_RX_BUDGET           32U

static UART_HandleTypeDef *console_uart;
static MCP2221A_CommandHandler console_command_handler;
static char console_line[MCP2221A_LINE_CAPACITY];
static size_t console_line_length;
static bool console_previous_was_cr;

static bool Console_Equals(const char *left, const char *right)
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

static void Console_ExecuteLine(void)
{
    char *command = console_line;
    char *end;

    console_line[console_line_length] = '\0';

    while (isspace((unsigned char)*command))
        command++;

    end = command + strlen(command);
    while ((end > command) && isspace((unsigned char)end[-1]))
        *--end = '\0';

    if (*command == '\0')
        return;

    if (Console_Equals(command, "HELP") || Console_Equals(command, "?"))
    {
        MCP2221A_Console_Print(
            "Commands:\r\n"
            "  HELP or ?  - show this list\r\n"
            "  PING       - verify PC-to-STM32 UART\r\n"
            "  INFO       - show bridge configuration\r\n"
            "  STATUS     - complete hub status\r\n"
            "  SENSORS    - current sensor values\r\n"
            "  NETWORK    - Wi-Fi, Blynk and weather status\r\n"
            "  MEMORY     - SDRAM and QSPI status\r\n");
    }
    else if (Console_Equals(command, "PING"))
    {
        MCP2221A_Console_Print("PONG - MCP2221A RX AND TX PASS\r\n");
    }
    else if (Console_Equals(command, "INFO"))
    {
        MCP2221A_Console_Print(
            "MCP2221A-I/SL USB-UART console\r\n"
            "USART3: 115200 baud, 8 data bits, no parity, 1 stop bit\r\n"
            "STM32 TX=PC10, STM32 RX=PC11\r\n");
    }
    else if (console_command_handler != NULL)
    {
        console_command_handler(command);
    }
    else
    {
        MCP2221A_Console_Print("Unknown command. Type HELP.\r\n");
    }
}

void MCP2221A_Console_Init(UART_HandleTypeDef *uart,
                           MCP2221A_CommandHandler command_handler)
{
    console_uart = uart;
    console_command_handler = command_handler;
    console_line_length = 0U;
    console_previous_was_cr = false;
}

void MCP2221A_Console_Process(void)
{
    uint8_t byte;
    uint32_t processed = 0U;

    if (console_uart == NULL)
        return;

    while ((processed < MCP2221A_RX_BUDGET) &&
           (HAL_UART_Receive(console_uart, &byte, 1U, 0U) == HAL_OK))
    {
        processed++;

        if ((byte == '\r') || (byte == '\n'))
        {
            if ((byte == '\n') && console_previous_was_cr)
            {
                console_previous_was_cr = false;
                continue;
            }

            console_previous_was_cr = (byte == '\r');
            MCP2221A_Console_Print("\r\n");
            Console_ExecuteLine();
            console_line_length = 0U;
            MCP2221A_Console_Print("> ");
            continue;
        }

        console_previous_was_cr = false;

        if ((byte == 8U) || (byte == 127U))
        {
            if (console_line_length > 0U)
            {
                console_line_length--;
                MCP2221A_Console_Print("\b \b");
            }
            continue;
        }

        if (isprint((unsigned char)byte) &&
            (console_line_length + 1U < MCP2221A_LINE_CAPACITY))
        {
            console_line[console_line_length++] = (char)byte;
            MCP2221A_Console_Write(&byte, 1U);
        }
    }
}

void MCP2221A_Console_Write(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if ((console_uart == NULL) || (bytes == NULL) || (length == 0U))
        return;

    while (length > 0U)
    {
        uint16_t chunk = (length > UINT16_MAX) ? UINT16_MAX : (uint16_t)length;
        uint32_t timeout = 20U + (uint32_t)chunk;

        if (HAL_UART_Transmit(console_uart,
                              (uint8_t *)bytes,
                              chunk,
                              timeout) != HAL_OK)
        {
            return;
        }

        bytes += chunk;
        length -= chunk;
    }
}

void MCP2221A_Console_Print(const char *text)
{
    if (text != NULL)
        MCP2221A_Console_Write(text, strlen(text));
}

void MCP2221A_Console_Printf(const char *format, ...)
{
    char buffer[MCP2221A_PRINTF_CAPACITY];
    va_list arguments;
    int length;

    if (format == NULL)
        return;

    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    if (length <= 0)
        return;

    if ((size_t)length >= sizeof(buffer))
        length = (int)sizeof(buffer) - 1;

    MCP2221A_Console_Write(buffer, (size_t)length);
}

void MCP2221A_Console_PrintBanner(void)
{
    MCP2221A_Console_Print(
        "\r\n"
        "========================================\r\n"
        " HOME HUB - MCP2221A DEBUG CONSOLE\r\n"
        " USART3 115200 8N1 | PC10 TX | PC11 RX\r\n"
        " Type HELP for commands\r\n"
        "========================================\r\n");
}
