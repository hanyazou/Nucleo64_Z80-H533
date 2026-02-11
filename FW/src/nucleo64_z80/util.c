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
#include "nucleo64_z80.h"
#include "core_cm33.h"

#include <stdio.h>
#include <ctype.h>

static uint32_t g_ticks_per_us;

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

void util_hexdump(const char *header, const void *addr, unsigned int size)
{
    char chars[17];
    const uint8_t *buf = addr;
    size = ((size + 15) & ~0xfU);
    for (int i = 0; i < size; i++) {
        if ((i % 16) == 0)
            printf("%s%04x:", header, i);
        printf(" %02x", buf[i]);
        if (0x20 <= buf[i] && buf[i] <= 0x7e) {
            chars[i % 16] = buf[i];
        } else {
            chars[i % 16] = '.';
        }
        if ((i % 16) == 15) {
            chars[16] = '\0';
            printf(" %s\n\r", chars);
        }
    }
}

void util_addrdump(const char *header, uint32_t addr_offs, const void *addr, unsigned int size)
{
    char chars[17];
    const uint8_t *buf = addr;
    size = ((size + 15) & ~0xfU);
    for (unsigned int i = 0; i < size; i++) {
        if ((i % 16) == 0)
            printf("%s%06lx:", header, addr_offs + i);
        printf(" %02x", buf[i]);
        if (0x20 <= buf[i] && buf[i] <= 0x7e) {
            chars[i % 16] = buf[i];
        } else {
            chars[i % 16] = '.';
        }
        if ((i % 16) == 15) {
            chars[16] = '\0';
            printf(" %s\n\r", chars);
        }
    }
}

void util_hexdump_sum(const char *header, const void *addr, unsigned int size)
{
    util_hexdump(header, addr, size);

    uint8_t sum = 0;
    const uint8_t *p = addr;
    for (int i = 0; i < size; i++)
        sum += *p++;
    printf("%s%53s CHECKSUM: %02x\n\r", header, "", sum);
}
