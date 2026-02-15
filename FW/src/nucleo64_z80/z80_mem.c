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

#include "z80.h"
#include "z80_pins.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

// MMU
static int mmu_bank = 0;
static int mmu_num_banks = 0;
static uint32_t mmu_mem_size = 0;

// Put a unique signature for this address (includes bank bits).
static void mem_addr_signature(uint32_t addr, uint8_t *buf, unsigned int len)
{
    len = (len / 8) * 8;
    while (len) {
        buf[0] = (addr >>  0) & 0xff;
        buf[1] = (addr >>  8) & 0xff;
        buf[2] = (addr >> 16) & 0xff;
        buf[3] = 0xa5;
        buf[4] = (addr >>  0) & 0xff;
        buf[5] = (addr >>  8) & 0xff;
        buf[6] = (addr >> 16) & 0xff;
        buf[7] = 0x5a;
        buf += 8;
        len -= 8;
    }
}

void mem_init()
{
    uint32_t addr;
    const unsigned int TMP_BUF_SIZE = 256;
    const unsigned int MAX_MEM_SIZE = 512 * 1024;
    const unsigned int MEM_CHECK_UNIT = 1024;
    uint8_t expected[TMP_BUF_SIZE];
    uint8_t actual[TMP_BUF_SIZE];

    // Z80 RAM check:
    //  - detect usable memory size
    //  - verify bank switching and fixed bank mapping

    // Stage 1: Determine usable SRAM size (up to MAX_MEM_SIZE).
    // Write a small signature to each candidate address (addr includes bank bits A16/A17),
    // then stop when the signature at 'addr' matches the signature at 0x00000 (wrap-around).
    for (addr = 0; addr < MAX_MEM_SIZE; addr += MEM_CHECK_UNIT) {
        printf("Memory 000000 - %06XH\r", (unsigned int)addr);

        // Write/read-back at 'addr' to confirm this address is writable.
        mem_addr_signature(addr, expected, sizeof(expected));
        mem_write_ram(addr, expected, sizeof(expected));
        mem_read_ram(addr, actual, sizeof(actual));
        if (memcmp(expected, actual, sizeof(expected)) != 0) {
            printf("\nMemory error at %06lXH\n\r", addr);
            util_addrdump("expect: ", addr, expected, sizeof(expected));
            util_addrdump("actual: ", addr, actual, sizeof(actual));
            break;
        }

        // Capacity probe: stop when the signature at 'addr' matches 0x00000 (wrap-around).
        if (addr == 0) continue;
        mem_read_ram(0, actual, sizeof(actual));
        if (memcmp(expected, actual, sizeof(actual)) == 0) {
            // Wrap-around: 'addr' maps back to the first region -> end of usable SRAM.
            break;
        }
    }
    mmu_mem_size = addr;
    mmu_num_banks = (int)(mmu_mem_size / 0x10000);

    // Stage 2: Validate the banked window (Z80 0000-BFFF) within the detected size.
    // Only this region is bank-switchable from the MCU; Z80 C000-FFFF is forced to bank0
    // by external logic, so accesses with (addr & 0xC000) are intentionally skipped here.
    for (addr = 0; addr < mmu_mem_size; addr += MEM_CHECK_UNIT) {
        printf("Memory 000000 - %06lXH\r", addr);

        // Skip 0x00000 (reference) and Z80 C000-FFFF (bank-fixed by hardware).
        if (addr == 0 || (addr & 0xc000)) continue;

        // Expected signature written in Stage 1.
        mem_addr_signature(addr, expected, sizeof(expected));

        mem_read_ram(addr, actual, sizeof(actual));
        if (memcmp(expected, actual, sizeof(actual)) != 0) {
            printf("\nMemory error at %06lXH\n\r", addr);
            util_addrdump("expect: ", addr, expected, sizeof(expected));
            util_addrdump("actual: ", addr, actual, sizeof(actual));
            while (1);
        }
    }

    // Stage 3: Confirm C000-FFFF is bank0-fixed (bank bits are ignored there).
    // Writing via 0x1C000 (bank1 + C000) should affect the same physical memory as 0x0C000,
    // so after writing different patterns, reading 0x0C000 must match the 0x1C000 pattern.
    for (uint32_t bank_addr = 0x10000; bank_addr < mmu_mem_size; bank_addr += 0x10000) {
        for (addr = 0x0c000; addr < 0x10000; addr += MEM_CHECK_UNIT) {
            printf("Memory 000000 - %06lXH\r", addr);

            // Write a signature via bank0-fixed C000 region.
            mem_addr_signature(addr, expected, sizeof(expected));
            mem_write_ram(addr, expected, sizeof(expected));

            // Overwrite via bankN+C000; should hit the same physical region.
            mem_addr_signature(bank_addr + addr, expected, sizeof(expected));
            mem_write_ram(bank_addr + addr, expected, sizeof(expected));

            // Read back from 0x0C000: must match the last write (bank bits ignored here).
            mem_read_ram(addr, actual, sizeof(actual));
            if (memcmp(expected, actual, sizeof(expected)) != 0) {
                printf("\nMemory error at %06lXH\n\r", addr);
                util_addrdump("expect: ", 0x0c000, expected, sizeof(expected));
                util_addrdump("actual: ", 0x0c000, actual, sizeof(actual));
                while (1);
            }
        }
    }

    printf("Memory 000000 - %06XH %d KB OK\r\n", (unsigned int)addr, (int)(mmu_mem_size / 1024));
}

void mem_write_ram(uint32_t addr, const void *buf, unsigned int len)
{
    set_bank_pins(addr);
    mem_write_z80_ram((uint16_t)addr, buf, len);
    set_bank_pins((uint32_t)mmu_bank << 16);
}

void mem_read_ram(uint32_t addr, void *buf, unsigned int len)
{
    set_bank_pins(addr);
    mem_read_z80_ram((uint16_t)addr, buf, len);
    set_bank_pins((uint32_t)mmu_bank << 16);
}

bool mem_verify_ram(uint32_t addr, const void *buf, unsigned int len)
{
    int errors = 0;
    set_memrq_pin(PIN_ACTIVE);
    set_data_dir(PIN_DIR_INPUT);
    while (len--) {
        set_addr_pins(addr);
        set_rd_pin(PIN_ACTIVE);
        delay_us(1);
        uint8_t data = data_pins();
        if (data != *(uint8_t*)buf) {
            printf("ERROR at %04X, %02X != %02X\r\n", (unsigned int)addr, data, *(uint8_t*)buf);
            if (5 < ++errors) break;
        }
        addr++;
        buf++;
        set_rd_pin(PIN_INACTIVE);
    }
    set_rd_pin(PIN_INACTIVE);
    set_memrq_pin(PIN_INACTIVE);
    set_bank_pins((uint32_t)mmu_bank << 16);
    set_data_dir(PIN_DIR_INPUT);

    return errors == 0 ? true : false;
}

void mem_write_z80_ram(uint16_t addr, const void *buf, unsigned int len)
{
    set_memrq_pin(PIN_ACTIVE);
    set_data_dir(PIN_DIR_OUTPUT);
    while (len--) {
        set_addr_pins(addr++);
        set_data_pins(*(uint8_t*)buf++);
        set_wr_pin(PIN_ACTIVE);
        delay_us(1);
        set_wr_pin(PIN_INACTIVE);
    }
    set_memrq_pin(PIN_INACTIVE);
    set_data_dir(PIN_DIR_INPUT);
}

void mem_read_z80_ram(uint16_t addr, void *buf, unsigned int len)
{
    set_memrq_pin(PIN_ACTIVE);
    set_data_dir(PIN_DIR_INPUT);
    while (len--) {
        set_addr_pins(addr++);
        set_rd_pin(PIN_ACTIVE);
        delay_us(1);
        *(uint8_t*)buf++ = data_pins();
        set_rd_pin(PIN_INACTIVE);
    }
    set_memrq_pin(PIN_INACTIVE);
    set_data_dir(PIN_DIR_INPUT);
}

void mmu_bank_config(int nbanks)
{
    #ifdef CPM_MMU_DEBUG
    printf("mmu_bank_config: %d\n\r", nbanks);
    #endif
    if (mmu_num_banks < nbanks)
        printf("WARNING: too many banks requested. (request is %d)\n\r", nbanks);
}

void mmu_bank_select(int bank)
{
    #ifdef CPM_MMU_DEBUG
    printf("mmu_bank_select: %d\n\r", bank);
    #endif
    if (mmu_bank == bank)
        return;
    if (mmu_num_banks <= bank) {
        static unsigned char first_time = 1;
        #if !defined(CPM_MMU_DEBUG)
        if (first_time)
        #endif
        {
            first_time = 0;
            printf("ERROR: bank %d is not available.\n\r", bank);
        }
    }
    mmu_bank = bank;
    set_bank_pins((uint32_t)mmu_bank << 16);
}
