/*
 * Copyright (c) 2024-2026 @hanyazou
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

#pragma once

#include "main.h"

#include "stm32h5xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAT(x, y) _CAT(x, y)
#define _CAT(x, y) x ## y
#define set_pin(pin, v) ((v) ? (CAT(pin,_GPIO_Port)->ODR |= CAT(pin,_Pin)) : \
                         (CAT(pin,_GPIO_Port)->ODR &= ~CAT(pin,_Pin)))
#define get_pin(pin) ((CAT(pin,_GPIO_Port)->IDR & CAT(pin,_Pin)) ? 1 : 0)

/* MODER: 2 bits per pin, field = [2n+1:2n].
* GPIO_PIN_x is 1-hot (1u<<n), so use ctz() to get pin index.
* Intended for compile-time constant pin masks (non-zero, 1-hot).
*/
#define MODER_FIELD_LSB(m) (1u << (__builtin_ctz((uint32_t)(m)) * 2u))
#define MODER_FIELD_MASK(m) (MODER_FIELD_LSB(m) * 3u)
#define set_pin_dir(pin, v) \
    ((v) ? \
     (CAT(pin,_GPIO_Port)->MODER &= ~MODER_FIELD_MASK(CAT(pin,_Pin))) : \
     (CAT(pin,_GPIO_Port)->MODER |= MODER_FIELD_LSB(CAT(pin,_Pin))))

typedef uint8_t __bit;

static inline void set_mask16(volatile uint16_t *r, uint16_t v, uint16_t m) {
    *r = (*r & ~m) | (v & m);
}

static inline void set_mask32(volatile uint32_t *r, uint32_t v, uint32_t m) {
    *r = (*r & ~m) | (v & m);
}

static inline uint8_t data_pins(void) { return (uint8_t)(GPIOB->IDR & 0x00ff); }
static inline void set_data_pins(uint8_t v) { GPIOB->ODR = ((GPIOB->ODR & 0xff00) | ((uint16_t)(v))); }
static inline void set_data_dir(uint8_t v) {
    GPIOB->MODER = ((GPIOB->MODER & 0xffff0000) | ((v) ? 0 : 0x00005555));
}
static inline uint16_t addr_pins(void) {
    return (uint16_t)((GPIOB->IDR & 0xe000) | (GPIOC->IDR & 0x1fff));
}
static inline void set_addr_pins(uint16_t v) {
    set_mask32(&GPIOB->ODR, v, 0xe000);  // PB13 ~ PB15
    set_mask32(&GPIOC->ODR, v, 0x1fff);  // PC0 ~ PC12
}
static inline void set_addr_dir(uint8_t v) {
    set_mask32(&GPIOB->MODER, v ? 0UL : 0x55555555UL, 0xfc000000);  // PB13 ~ PA15
    set_mask32(&GPIOC->MODER, v ? 0UL : 0x55555555UL, 0x03ffffff);  // PC0 ~ PC12
}
static inline void set_bank_pins(uint32_t addr) {
    set_pin(BANK_SEL0, (addr & (1lu << 16) ? 1 : 0));  // A16
    set_pin(BANK_SEL1, (addr & (1lu << 17) ? 0 : 1));  // A17 inverted to enable TC551001 CE2
}

#define PIN_DIR_OUTPUT 0
#define PIN_DIR_INPUT 1
#define PIN_HIGH 1
#define PIN_LOW 0
#define PIN_ACTIVE 0
#define PIN_INACTIVE 1

static inline __bit ioreq_pin(void) { return get_pin(Z80_IOREQ); }
//static inline __bit memrq_pin(void) { return get_pin(Z80_MEMRQ); }
static inline __bit wait_pin(void) { return get_pin(Z80_WAIT); }
//static inline __bit rd_pin(void) { return get_pin(Z80_RD); }
static inline __bit wr_pin(void) { return get_pin(Z80_WR); }
static inline void set_ioreq_pin(uint8_t v) { set_pin(Z80_IOREQ, v); }
static inline void set_memrq_pin(uint8_t v) { set_pin(Z80_MEMRQ, v); }
static inline void set_rd_pin(uint8_t v) { set_pin(Z80_RD, v); }
static inline void set_wr_pin(uint8_t v) { set_pin(Z80_WR, v); }
static inline void set_busrq_pin(uint8_t v) { set_pin(Z80_BUSRQ, v); }
static inline void set_reset_pin(uint8_t v) { set_pin(Z80_RESET, v); }
static inline void set_wait_pin(uint8_t v) { set_pin(Z80_WAIT, v); }
static inline void set_wait_pin_dir(uint8_t v) { set_pin_dir(Z80_WAIT, v); }

void bus_master(int enable);
void z80_init_pins(void);
void z80_deinit_pins(void);

#ifdef __cplusplus
}
#endif
