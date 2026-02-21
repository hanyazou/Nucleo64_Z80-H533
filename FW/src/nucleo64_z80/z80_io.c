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

#include "nucleo64_z80.h"
#include "nucleo64_config.h"
#include "disk_drive.h"
#include "util.h"
#include "z80.h"
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

#define DEBUG_DISK 0
#define DEBUG_DISK_READ 0
#define DEBUG_DISK_WRITE 0

static struct disk_status {
    uint8_t stat;
    uint8_t drive;
    uint8_t track;
    uint16_t sector;
    uint8_t op;
    uint8_t dmal;
    uint8_t dmah;
    uint8_t *ptr;
    uint8_t buf[SECTOR_SIZE];
} disk_ = {};
static struct disk_status *disk = &disk_;

// hardware control
static uint8_t hw_ctrl_lock = HW_CTRL_LOCKED;

static uint8_t hw_ctrl_read(void)
{
    return hw_ctrl_lock;
}

static void hw_ctrl_write(uint8_t val)
{
    if (hw_ctrl_lock != HW_CTRL_UNLOCKED && val != HW_CTRL_MAGIC)
        return;
    if (val == HW_CTRL_MAGIC) {
        hw_ctrl_lock = HW_CTRL_UNLOCKED;
        return;
    }
    if (val & HW_CTRL_RESET) {
        printf("\r\nReset by IO port %02XH\r\n", HW_CTRL);
        HAL_NVIC_SystemReset();
    }
    if (val & HW_CTRL_HALT) {
        printf("\r\nHALT by IO port %02XH\r\n", HW_CTRL);
        while (1);
    }
}

static void do_disk_io(void)
{
    disk->stat = DISK_ST_ERROR;
    uint16_t addr = ((uint16_t)disk->dmah << 8) | disk->dmal;

    if (disk->op == DISK_OP_DMA_WRITE) {
        // transfer write data from SRAM to the buffer
        // Note: writing data 128 bytes are in the buffer already in non DMA write case
        mem_read_z80_ram(addr, disk->buf, SECTOR_SIZE);
    }

    struct z80_pin_state pin_state;
    z80_release_pins(&pin_state);
    sd_spi_acquire();

    if (disk->op == DISK_OP_DMA_READ || disk->op == DISK_OP_READ) {
        //
        // DISK read
        //

        // read from the DISK
        if (!disk_drive_read(disk->drive, disk->track, disk->sector, disk->buf, SECTOR_SIZE)) {
            printf("%s: read error at %u/%u/%u\r\n", __func__, disk->drive, disk->track,
                   disk->sector);
            goto release_spi_pis;
        }

        // Store disk I/O status here so that io_invoke_target_cpu() can return the status in it
        disk->stat = DISK_ST_SUCCESS;

        if (disk->op == DISK_OP_DMA_READ) {
            //
            // DMA read
            //
            // transfer read data to SRAM
            sd_spi_release();
            z80_acquire_pins(&pin_state);
            mem_write_z80_ram(addr, disk->buf, SECTOR_SIZE);
            disk->ptr = NULL;
            goto disk_io_done;
        } else {
            //
            // non DMA read
            //

            // just set the read pointer to the heat of the buffer
            disk->ptr = disk->buf;
        }
    } else
    if (disk->op == DISK_OP_DMA_WRITE || disk->op == DISK_OP_WRITE) {
        //
        // DISK write
        //

        // write buffer to the DISK
        if (!disk_drive_write(disk->drive, disk->track, disk->sector, disk->buf, SECTOR_SIZE)) {
            printf("%s: read error at %u/%u/%u\r\n", __func__, disk->drive, disk->track,
                   disk->sector);
            goto release_spi_pis;
        }
        disk->stat = DISK_ST_SUCCESS;
        disk->ptr = NULL;
    } else {
        disk->stat = DISK_ST_ERROR;
        disk->ptr = NULL;
    }

 release_spi_pis:
    sd_spi_release();
    z80_acquire_pins(&pin_state);

 disk_io_done:
    if ((DEBUG_DISK_READ  && (disk->op == DISK_OP_DMA_READ  || disk->op == DISK_OP_READ )) ||
        (DEBUG_DISK_WRITE && (disk->op == DISK_OP_DMA_WRITE || disk->op == DISK_OP_WRITE))) {
        printf("DISK: OP=%02x D/T/S=%d/%3d/%3d ADDR=%02x%02x ... ST=%02x\r\n",
               disk->op, disk->drive, disk->track, disk->sector,
               disk->dmah, disk->dmal, disk->stat);
    }
}

void io_handle()
{
    uint16_t addr;
    uint8_t data;
    bool io_write, io_read;
    bool do_bus_master = false;
    bool io_handled = false;

    addr = (addr_pins() & 0xff);
    io_write = (wr_pin() == PIN_ACTIVE);
    if (io_write) data = data_pins();
    io_read = ! io_write;

    switch (addr) {
    //
    // serial console
    //
    case UART_DREG:
        if (io_write) {
            write(0, &data, 1);
        } else {
            data = getchar();
        }
        io_handled = true;
        break;
    case UART_CREG:
        if (io_read) {
            data = input_key_available() ? 0xff : 0x00;
            io_handled = true;
        }
        break;

    //
    // disk I/O
    //
    case DISK_REG_DATA:
        if (io_write) {
            if (disk->ptr && (disk->ptr - disk->buf) < SECTOR_SIZE) {
                *disk->ptr++ = data;
                if (disk->op == DISK_OP_WRITE && (disk->ptr - disk->buf) == SECTOR_SIZE) {
                    do_bus_master = 1;
                }
            } else
            if (DEBUG_DISK) {
                printf("DISK: OP=%02x D/T/S=%d/%3d/%3d ADDR=%02x%02x (WR IGNORED)\r\n",
                       disk->op, disk->drive, disk->track, disk->sector, disk->dmah, disk->dmal);
            }
        } else {
            if (disk->ptr && (disk->ptr - disk->buf) < SECTOR_SIZE) {
                data = *disk->ptr++;
            } else
            if (DEBUG_DISK) {
                printf("DISK: OP=%02x D/T/S=%d/%3d/%3d ADDR=%02x%02x (RD IGNORED)\r\n",
                       disk->op, disk->drive, disk->track, disk->sector, disk->dmah, disk->dmal);
            }
        }
        io_handled = true;
        break;
    case DISK_REG_DRIVE:
        if (io_write) {
            disk->drive = data;
            io_handled = true;
        }
        break;
    case DISK_REG_TRACK:
        if (io_write) {
            disk->track = data;
            io_handled = true;
        }
        break;
    case DISK_REG_SECTOR:
        if (io_write) {
            disk->sector = (disk->sector & 0xff00) | data;
            io_handled = true;
        }
        break;
    case DISK_REG_SECTORH:
        if (io_write) {
            disk->sector = (disk->sector & 0x00ff) | ((uint16_t)data << 8);
            io_handled = true;
        }
        break;
    case DISK_REG_FDCOP:
        if (io_write) {
            disk->op = data;
            if (disk->op == DISK_OP_WRITE) {
                disk->ptr = disk->buf;
            } else {
                do_bus_master = 1;
            }
            if ((DEBUG_DISK_READ  && (disk->op == DISK_OP_DMA_READ  || disk->op == DISK_OP_READ )) ||
                (DEBUG_DISK_WRITE && (disk->op == DISK_OP_DMA_WRITE || disk->op == DISK_OP_WRITE))) {
                printf("DISK: OP=%02x D/T/S=%d/%3d/%3d            ADDR=%02x%02x ... \r\n",
                       disk->op, disk->drive, disk->track, disk->sector, disk->dmah, disk->dmal);
            }
            io_handled = true;
        }
        break;
    case DISK_REG_FDCST:
        if (io_read) {
            data = disk->stat;
            io_handled = true;
        }
        break;
    case DISK_REG_DMAL:
        if (io_write) {
            disk->dmal = data;
            io_handled = true;
        }
        break;
    case DISK_REG_DMAH:
        if (io_write) {
            disk->dmah = data;
            io_handled = true;
        }
        break;

    //
    // MMU and HW control
    //
    case MMU_INIT:
    case MMU_BANK_SEL:
        if (io_write) {
            do_bus_master = 1;
            io_handled = true;
        }
        break;
    case HW_CTRL:
        if (io_write) {
            hw_ctrl_write(data);
        } else {
            data = hw_ctrl_read();
        }
        io_handled = true;
        break;
    }

    if (!io_handled) {
        if (io_write) {
            printf("%s: write %02X, %02X %c\r\n", __func__, addr & 0xff, data,
                   (0x30 <= data && data <= 0x7f) ? data : ' ');
        } else {
            printf("%s:  read %02X\r\n", __func__, addr & 0xff);
            data = 0xff;
        }
        printf("%s: halt.\r\n", __func__);
        while(1);
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
    if (do_bus_master) {
        bus_master(1);

        switch (addr) {
        case DISK_REG_DATA:
        case DISK_REG_FDCOP:
            do_disk_io();
            break;
        case MMU_INIT:
            if (io_write) {
                mmu_bank_config(data);
            }
            break;
        case MMU_BANK_SEL:
            if (io_write) {
                mmu_bank_select(data);
            }
            break;
        }

        bus_master(0);
    }

    // Release data bus for I/O read cycles
    if (!do_bus_master && io_read) {
        set_data_dir(PIN_DIR_INPUT);
    }

    // Release BUSRQ to let the Z80 resume execution
    set_busrq_pin(PIN_INACTIVE);
}
