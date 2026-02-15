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

#include "util.h"
#include "z80_pins.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#define IOBASE 0
#define UART_CREG        (IOBASE+0)     // 00h Control REG
#define UART_DREG        (IOBASE+1)     // 01h Data REG
#define IO_PRNSTA        (IOBASE+2)     // 02h printer status
#define IO_PRNDAT        (IOBASE+3)     // 03h printer data
#define IO_AUXSTA        (IOBASE+4)     // 04h auxiliary status
#define IO_AUXDAT        (IOBASE+5)     // 05h auxiliary data
#define DISK_REG_DATA    (IOBASE+8)     // 08h fdc-port: data (non-DMA)
#define DISK_REG_DRIVE   (IOBASE+10)    // 0Ah fdc-port: # of drive
#define DISK_REG_TRACK   (IOBASE+11)    // 0Bh fdc-port: # of track
#define DISK_REG_SECTOR  (IOBASE+12)    // 0Ch fdc-port: # of sector
#define DISK_REG_FDCOP   (IOBASE+13)    // 0Dh fdc-port: command
#define DISK_OP_DMA_READ     0
#define DISK_OP_DMA_WRITE    1
#define DISK_OP_READ         2
#define DISK_OP_WRITE        3
#define DISK_REG_FDCST   (IOBASE+14)    // OEh fdc-port: status
#define DISK_ST_SUCCESS      0x00
#define DISK_ST_ERROR        0x01
#define DISK_REG_DMAL    (IOBASE+15)    // OFh dma-port: dma address low
#define DISK_REG_DMAH    (IOBASE+16)    // 10h dma-port: dma address high
#define DISK_REG_SECTORH (IOBASE+17)    // 11h fdc-port: # of sector high

#define MMU_INIT         (IOBASE+20)    // 14h MMU initialisation
#define MMU_BANK_SEL     (IOBASE+21)    // 15h MMU bank select
#define MMU_SEG_SIZE     (IOBASE+22)    // 16h MMU select segment size (in pages a 256 bytes)
#define MMU_WR_PROT      (IOBASE+23)    // 17h MMU write protect/unprotect common memory segment

#define HW_CTRL          160            // A0h hardware control
#define HW_CTRL_LOCKED       0xff
#define HW_CTRL_UNLOCKED     0x00
#define HW_CTRL_MAGIC        0xaa
#define HW_CTRL_RESET        (1 << 6)
#define HW_CTRL_HALT         (1 << 7)

void io_handle()
{
    uint16_t addr;
    uint8_t data;
    bool io_write, io_read;

    addr = addr_pins();
    io_write = (wr_pin() == PIN_ACTIVE);
    if (io_write) data = data_pins();
    io_read = ! io_write;

    switch (addr & 0xff) {
    case UART_DREG:
        if (io_write) {
            write(0, &data, 1);
        } else {
            data = getchar();
        }
        break;
    case UART_CREG:
        if (io_read) {
            data = input_key_available() ? 0x03 : 0x02;
        }
        break;
    default:
        if (io_write) {
            printf("%s: write %02X, %02X %c\r\n", __func__, addr & 0xff, data,
                   (0x30 <= data && data <= 0x7f) ? data : ' ');
        } else {
            printf("%s:  read %02X\r\n", __func__, addr & 0xff);
            data = 0xff;
        }
        break;
    }

    if (io_read) {
        set_data_pins(data);
        set_data_dir(PIN_DIR_OUTPUT);
    }

    // Complete the current Z80 I/O cycle under MCU control.
    //
    // The Z80 is currently held in WAIT during an I/O access.
    // The following sequence (steps 1–3) completes exactly one
    // I/O machine cycle while keeping the CPU under MCU control:
    //
    //   Step 1: Assert BUSRQ so the Z80 will relinquish the bus
    //           immediately after the current I/O cycle completes.
    //
    //   Step 2: Release WAIT to allow the pending I/O cycle
    //           (read or write) to proceed and complete.
    //
    //   Step 3: Wait for IORQ to be deasserted, confirming that
    //           the I/O cycle has fully finished, then restore
    //           bus signals to a safe (Hi-Z) state.
    //
    // After step 3, the I/O cycle is complete and the Z80 is stopped
    // with BUSRQ asserted. From this point on, the MCU owns the bus
    // and may perform additional operations (e.g. DMA, memory access,
    // or internal processing).
    //
    // Note:
    // BUSRQ is sampled at machine cycle boundaries.
    // By asserting BUSRQ before releasing WAIT, we ensure
    // the CPU stops immediately after this I/O cycle.

    // Step 1: Request the bus so the Z80 stops after this I/O cycle
    set_busrq_pin(PIN_ACTIVE);

    // Step 2: Release WAIT to let the I/O cycle complete
    set_wait_pin(PIN_INACTIVE);
    set_wait_pin_dir(PIN_DIR_OUTPUT);

    // Step 3: Wait for the end of the I/O cycle (IORQ deasserted)
    while (ioreq_pin() == PIN_ACTIVE) {
        ;
    }
    // Return WAIT to Hi-Z
    set_wait_pin_dir(PIN_DIR_INPUT);

    //
    // MCU owns the bus here
    //

    // Release data bus for I/O read cycles
    if (io_read) {
        set_data_dir(PIN_DIR_INPUT);
    }

    // Release BUSRQ to let the Z80 resume execution
    set_busrq_pin(PIN_INACTIVE);
}
