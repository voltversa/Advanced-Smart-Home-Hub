#ifndef MCP2221A_CONSOLE_H
#define MCP2221A_CONSOLE_H

#include "main.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The MCP2221A requires no STM32-side protocol driver. It is a transparent
 * USB-to-UART bridge connected to USART3:
 *
 *   STM32 PC10 / USART3_TX -> MCP2221A URX
 *   STM32 PC11 / USART3_RX <- MCP2221A UTX
 *
 * This module provides a single debug/logging API and a small command line.
 */

typedef void (*MCP2221A_CommandHandler)(const char *command);

void MCP2221A_Console_Init(UART_HandleTypeDef *uart,
                           MCP2221A_CommandHandler command_handler);
void MCP2221A_Console_Process(void);

void MCP2221A_Console_Write(const void *data, size_t length);
void MCP2221A_Console_Print(const char *text);
void MCP2221A_Console_Printf(const char *format, ...);
void MCP2221A_Console_PrintBanner(void);

#endif /* MCP2221A_CONSOLE_H */
