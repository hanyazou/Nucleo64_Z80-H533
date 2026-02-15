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
#include "z80_pins.h"
#include "sd_disk.h"

#include <stdio.h>
#include <stdint.h>

static size_t rom_basic_size;
static const uint8_t rom_basic[];
static size_t rom_ipl_size;
static const uint8_t rom_ipl[];

static struct z80_pin_state g_pin_state;

void z80_poweron(void)
{
    // Put all Z80-related GPIOs into Hi-Z to avoid back-powering
    set_reset_pin(0);
    set_busrq_pin(0);
    set_bank_pins(0);
    z80_release_pins(&g_pin_state);

    // Perform a full power-cycle reset of the Z80.
    set_pin(Z80_PWR_EN, PIN_LOW);  // turn off the 5V supply
    delay_ms(200);  // Keep power off long enough for a full reset

    // Supply 5V power to the Z80
    set_pin(Z80_PWR_EN, PIN_HIGH);
    delay_ms(10);
}

void z80_init(void)
{
    // Reconfigure Z80 control and address pins after power-on
    z80_acquire_pins(&g_pin_state);

    // Acquire the Z80 bus
    set_busrq_pin(PIN_ACTIVE);
    set_reset_pin(PIN_INACTIVE);
    delay_ms(10);  // Wait for BUSACK (allow extra time for the slow clock)
    bus_master(1);

    mem_init();

    // Load the initial program into RAM and verify it
    const uint8_t *rom;
    size_t rom_size;

    if (sd_disk_have_boot_drive()) {
        rom = rom_ipl;
        rom_size = rom_ipl_size;
    } else {
        rom = rom_basic;
        rom_size = rom_basic_size;
    }
    mem_write_ram(0, rom, rom_size);
    if (!mem_verify_ram(0, rom, rom_size)) while (1);
}

void z80_run(void)
{
    // Release the bus and start the Z80
    set_wait_pin_dir(PIN_DIR_INPUT);
    bus_master(0);
    set_reset_pin(PIN_ACTIVE);
    set_busrq_pin(PIN_INACTIVE);
    delay_ms(1);
    set_reset_pin(PIN_INACTIVE);

    /*
     * Z80 run loop
     */
    while (1) {
        // wait for I/O request
        while (wait_pin() != PIN_ACTIVE) { }

        // should never happen: IOREQ must be active here
        if (ioreq_pin() != PIN_ACTIVE) {
            printf("%s: IOREQ is not active\r\n", __func__);
            while (1);
        }

        // handle I/O request
        io_handle();
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

static const uint8_t rom_basic[] = {
#include "emubasic.inc"
};
static size_t rom_basic_size = sizeof(rom_basic);

static const uint8_t rom_ipl[] = {
#include "ipl.inc"
};
static size_t rom_ipl_size = sizeof(rom_ipl);
