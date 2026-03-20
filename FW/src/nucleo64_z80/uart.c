/*
 * Copyright (c) 2023-2026 @hanyazou
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nucleo64_z80.h"
//#include "main.h"

#include <stdio.h>

#define RX_BUF_SIZE 128

static UART_HandleTypeDef *g_huart;
static uint8_t g_uart_rx_ch;
static volatile uint8_t g_rx_buf[RX_BUF_SIZE];
static volatile uint16_t g_rx_w = 0;
static volatile uint16_t g_rx_r = 0;

int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(g_huart, (uint8_t*)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}

void uart_start(UART_HandleTypeDef *huart)
{
    g_huart = huart;
    HAL_UART_Receive_IT(g_huart, &g_uart_rx_ch, 1);
}

static inline void rx_buf_push(uint8_t ch)
{
    uint16_t next = (g_rx_w + 1) % RX_BUF_SIZE;
    if (next != g_rx_r) {   // overflow discard
        g_rx_buf[g_rx_w] = ch;
        g_rx_w = next;
    }
}

static inline int rx_buf_pop(uint8_t *ch)
{
    if (g_rx_r == g_rx_w)
        return 0;   // empty
    *ch = g_rx_buf[g_rx_r];
    g_rx_r = (g_rx_r + 1) % RX_BUF_SIZE;
    return 1;
}

void uart_rx_callback(UART_HandleTypeDef *huart)
{
    if (huart == g_huart) {
        rx_buf_push(g_uart_rx_ch);
        HAL_UART_Receive_IT(g_huart, &g_uart_rx_ch, 1);
    }
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    if (len <= 0) return 0;

    uint8_t ch;
    while (!rx_buf_pop(&ch)) {
        tx_thread_sleep(1);  // or __WFI();
    }

    *ptr = (char)ch;
    return 1;
}

int input_key_available(void)
{
    return g_rx_r != g_rx_w;
}
