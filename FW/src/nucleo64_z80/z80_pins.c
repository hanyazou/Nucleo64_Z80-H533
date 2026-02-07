/*
 * Copyright (c) 2026 @hanyazou
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

#include "z80_pins.h"

void z80_init_pins(void)
{
    set_pin(Z80_NMI, PIN_INACTIVE);
    set_pin(Z80_INT, PIN_INACTIVE);
    set_reset_pin(PIN_ACTIVE);
    set_busrq_pin(PIN_INACTIVE);
    set_bank_pins(0);
    set_pin_dir(Z80_NMI, PIN_DIR_OUTPUT);
    set_pin_dir(Z80_INT, PIN_DIR_OUTPUT);
    set_pin_dir(Z80_RESET, PIN_DIR_OUTPUT);
    set_pin_dir(Z80_BUSRQ, PIN_DIR_OUTPUT);
    set_pin_dir(BANK_SEL0, PIN_DIR_OUTPUT);
    set_pin_dir(BANK_SEL1, PIN_DIR_OUTPUT);
    bus_master(0);
}

void z80_deinit_pins(void)
{
    set_pin_dir(Z80_NMI, PIN_DIR_INPUT);
    set_pin_dir(Z80_INT, PIN_DIR_INPUT);
    set_pin_dir(Z80_RESET, PIN_DIR_INPUT);
    set_pin_dir(Z80_BUSRQ, PIN_DIR_INPUT);
    set_pin_dir(BANK_SEL0, PIN_DIR_INPUT);
    set_pin_dir(BANK_SEL1, PIN_DIR_INPUT);
    bus_master(0);
}

void bus_master(int enable)
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
