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

#include "main.h"
#include "util.h"
#include "z80.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

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

static uint8_t data_pins(void) { return (uint8_t)(GPIOB->IDR & 0x00ff); }
static void set_data_pins(uint8_t v) { GPIOB->ODR = ((GPIOB->ODR & 0xff00) | ((uint16_t)(v))); }
static void set_data_dir(uint8_t v) {
    GPIOB->MODER = ((GPIOB->MODER & 0xffff0000) | ((v) ? 0 : 0x00005555));
}
static uint16_t addr_pins(void) {
    return (uint16_t)((GPIOB->IDR & 0xe000) | (GPIOC->IDR & 0x1fff));
}
static void set_addr_pins(uint16_t v) {
    set_mask32(&GPIOB->ODR, v, 0xe000);  // PB13 ~ PB15
    set_mask32(&GPIOC->ODR, v, 0x1fff);  // PC0 ~ PC12
}
static void set_addr_dir(uint8_t v) {
    set_mask32(&GPIOB->MODER, v ? 0UL : 0x55555555UL, 0xfc000000);  // PB13 ~ PA15
    set_mask32(&GPIOC->MODER, v ? 0UL : 0x55555555UL, 0x03ffffff);  // PC0 ~ PC12
}

#define PIN_DIR_OUTPUT 0
#define PIN_DIR_INPUT 1
#define PIN_HIGH 1
#define PIN_LOW 0
#define PIN_ACTIVE 0
#define PIN_INACTIVE 1

static __bit ioreq_pin(void) { return get_pin(Z80_IOREQ); }
//static __bit memrq_pin(void) { return get_pin(Z80_MEMRQ); }
static __bit wait_pin(void) { return get_pin(Z80_WAIT); }
//static __bit rd_pin(void) { return get_pin(Z80_RD); }
static __bit wr_pin(void) { return get_pin(Z80_WR); }
static void set_ioreq_pin(uint8_t v) { set_pin(Z80_IOREQ, v); }
static void set_memrq_pin(uint8_t v) { set_pin(Z80_MEMRQ, v); }
static void set_rd_pin(uint8_t v) { set_pin(Z80_RD, v); }
static void set_wr_pin(uint8_t v) { set_pin(Z80_WR, v); }
static void set_busrq_pin(uint8_t v) { set_pin(Z80_BUSRQ, v); }
static void set_reset_pin(uint8_t v) { set_pin(Z80_RESET, v); }
static void set_wait_pin(uint8_t v) { set_pin(Z80_WAIT, v); }
static void set_wait_pin_dir(uint8_t v) { set_pin_dir(Z80_WAIT, v); }

static void bus_master(int enable)
{
    if (enable) {
        // Set address bus as output
        set_addr_dir(PIN_DIR_OUTPUT);

        // Set /IOREQ, /MEMRQ, /RD and /WR as output
        set_ioreq_pin(PIN_INACTIVE);
        set_memrq_pin(PIN_INACTIVE);
        set_rd_pin(PIN_INACTIVE);
        set_wr_pin(PIN_INACTIVE);
        set_wait_pin(PIN_INACTIVE);
        set_pin_dir(Z80_IOREQ, PIN_DIR_OUTPUT);
        set_pin_dir(Z80_MEMRQ, PIN_DIR_OUTPUT);
        set_pin_dir(Z80_RD, PIN_DIR_OUTPUT);
        set_pin_dir(Z80_WR, PIN_DIR_OUTPUT);
    } else {
        // Set address and data bus as input
        set_addr_dir(PIN_DIR_INPUT);
        set_data_dir(PIN_DIR_INPUT);

        // Set /IOREQ, /MEMRQ, /RD and /WR as input
        set_pin_dir(Z80_IOREQ, PIN_DIR_INPUT);
        set_pin_dir(Z80_MEMRQ, PIN_DIR_INPUT);
        set_pin_dir(Z80_RD, PIN_DIR_INPUT);
        set_pin_dir(Z80_WR, PIN_DIR_INPUT);
    }
}

#define UART_DREG 0x00		//Data REG
#define UART_CREG 0x01		//Control REG
extern size_t rom_size;
extern const uint8_t rom[];

void z80_init(void)
{
    uint16_t addr;
    uint8_t data;

    // Perform a full power-cycle reset of the Z80.
    // Put all Z80-related GPIOs into Hi-Z to avoid back-powering,
    // then turn off the 5V supply long enough to fully discharge the system.
    bus_master(0);
    set_pin_dir(Z80_NMI, PIN_DIR_INPUT);
    set_pin_dir(Z80_INT, PIN_DIR_INPUT);
    set_pin_dir(Z80_RESET, PIN_DIR_INPUT);
    set_pin_dir(Z80_BUSRQ, PIN_DIR_INPUT);
    set_pin_dir(BANK_SEL0, PIN_DIR_INPUT);
    set_pin_dir(BANK_SEL1, PIN_DIR_INPUT);

    // Power-cycle the Z80 for a complete reset
    set_pin(Z80_PWR_EN, PIN_LOW);
    delay_ms(200);  // Keep power off long enough for a full reset
    // Supply 5V power to the Z80
    set_pin(Z80_PWR_EN, PIN_HIGH);
    delay_ms(10);

    // Reconfigure Z80 control and address pins after power-on
    set_pin(Z80_NMI, PIN_INACTIVE);
    set_pin(Z80_INT, PIN_INACTIVE);
    set_reset_pin(PIN_ACTIVE);
    set_busrq_pin(PIN_INACTIVE);
    set_pin(BANK_SEL0, 0);  // A16
    set_pin(BANK_SEL1, 1);  // for TC551001 CE2 pin
    set_pin_dir(Z80_NMI, PIN_DIR_OUTPUT);
    set_pin_dir(Z80_INT, PIN_DIR_OUTPUT);
    set_pin_dir(Z80_RESET, PIN_DIR_OUTPUT);
    set_pin_dir(Z80_BUSRQ, PIN_DIR_OUTPUT);
    set_pin_dir(BANK_SEL0, PIN_DIR_OUTPUT);
    set_pin_dir(BANK_SEL1, PIN_DIR_OUTPUT);

    // Acquire the Z80 bus
    set_busrq_pin(PIN_ACTIVE);
    set_reset_pin(PIN_INACTIVE);
    delay_ms(10);  // Wait for BUSACK (allow extra time for the slow clock)
    bus_master(1);

    // Load the initial program into RAM
    addr = 0;
    set_memrq_pin(PIN_ACTIVE);
    set_data_dir(PIN_DIR_OUTPUT);
    set_addr_pins(addr);
    while (addr < rom_size) {
        set_data_pins(rom[addr]);
        set_wr_pin(PIN_ACTIVE);
        delay_us(1);
        set_wr_pin(PIN_INACTIVE);
        set_addr_pins(++addr);
    }

    // Verify RAM data
    addr = 0;
    set_data_dir(PIN_DIR_INPUT);
    set_addr_pins(addr);
    while (addr < rom_size) {
        set_rd_pin(PIN_ACTIVE);
        data = data_pins();
        if (data != rom[addr]) {
            printf("ERROR at %04X, %02X != %02X\r\n", addr, data, rom[addr]);
        }
        delay_us(1);
        set_rd_pin(PIN_INACTIVE);
        set_addr_pins(++addr);
    }
    set_memrq_pin(PIN_INACTIVE);
}

void z80_run(void)
{
    uint16_t addr;
    uint8_t data;
    int io_write;

    // Release the bus and start the Z80
    set_wait_pin_dir(PIN_DIR_INPUT);
    bus_master(0);
    set_busrq_pin(PIN_INACTIVE);

    /*
     * run Z80
     */
    while (1) {
        while (wait_pin() != PIN_ACTIVE) {
            ;
        }

        /*
         * handle I/O request
         */
        if (ioreq_pin() != PIN_ACTIVE) {
            printf("%s: IOREQ is not active\r\n", __func__);
            while (1);
        }

        addr = addr_pins();
        io_write = (wr_pin() == PIN_ACTIVE);

        if (io_write) {
            data = data_pins();
            switch (addr & 0xff) {
            case UART_DREG:
                write(0, &data, 1);
                break;
            default:
                printf("%s: write %02X, %02X %c\r\n", __func__, addr & 0xff, data,
                       (0x30 <= data && data <= 0x7f) ? data : ' ');
                break;
            }
        } else {
            switch (addr & 0xff) {
            case UART_DREG:
                data = getchar();
                break;
            case UART_CREG:
                data = input_key_available() ? 0x03 : 0x02;
                break;
            default:
                printf("%s:  read %02X\r\n", __func__, addr & 0xff);
                data = 0xff;
                break;
            }
            set_data_pins(data);
            set_data_dir(PIN_DIR_OUTPUT);
        }

        set_busrq_pin(PIN_ACTIVE);
        set_wait_pin(PIN_INACTIVE);
        set_wait_pin_dir(PIN_DIR_OUTPUT);
        while (ioreq_pin() == PIN_ACTIVE) {
            ;
        }
        set_wait_pin_dir(PIN_DIR_INPUT);
        if (!io_write) {
            set_data_dir(PIN_DIR_INPUT);
        }
        set_busrq_pin(PIN_INACTIVE);
    }

    /*
     * stop Z80
     */
    set_reset_pin(PIN_ACTIVE);
    delay_us(8);
    bus_master(1);

    printf("%s: halt.\r\n", __func__);
    while (1)
        HAL_Delay(10000);
}

const uint8_t rom[] = {
#include "emubasic.inc"
};
size_t rom_size = sizeof(rom);
