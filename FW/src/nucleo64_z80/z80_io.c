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
    int io_write;

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
