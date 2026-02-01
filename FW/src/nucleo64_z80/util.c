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

#include "util.h"
#include "main.h"
#include "core_cm33.h"

#include <unistd.h>

#define RX_BUF_SIZE 128

static UART_HandleTypeDef *g_huart;
static uint8_t g_uart_rx_ch;
static volatile uint8_t g_rx_buf[RX_BUF_SIZE];
static volatile uint16_t g_rx_w = 0;
static volatile uint16_t g_rx_r = 0;
static uint32_t g_ticks_per_us;

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
    if (!rx_buf_pop((uint8_t*)ptr)) return 0;
    return 1;
}

int input_key_available(void)
{
    return g_rx_r != g_rx_w;
}

void delay_init(void)
{
    /* Enable trace/debug block (needed on some cores to use DWT) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* Reset and start CYCCNT */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Cache cycles per microsecond (HCLK frequency) */
    g_ticks_per_us = HAL_RCC_GetHCLKFreq() / 1000000u;
    if (g_ticks_per_us == 0) g_ticks_per_us = 1;
}

void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * g_ticks_per_us;
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
        __NOP();
    }
}

void delay_ms(uint32_t ms)
{
    delay_us(ms*1000);
}
