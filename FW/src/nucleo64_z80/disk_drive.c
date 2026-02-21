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

#include "disk_drive.h"
#include "disk_file.h"
#include "nucleo64_z80.h"

#include <stdio.h>

typedef bool (*read_fn)(uint8_t drive, uint32_t lba, uint8_t *buf, int buf_len);
typedef bool (*write_fn)(uint8_t drive, uint32_t lba, uint8_t *buf, int buf_len);

struct disk_ops {
    read_fn read;
    read_fn write;
};

struct drive {
    unsigned int sectors;
    struct disk_ops *ops;
};

static struct disk_ops file_ops = {
    disk_file_read,
    disk_file_write,
};

static struct drive drives[] = {
    { 26, &file_ops },
    { 26, &file_ops },
    { 26, &file_ops },
    { 26, &file_ops },
    { 0 },
    { 0 },
    { 0 },
    { 0 },
    { 128, &file_ops },
    { 128, &file_ops },
    { 128, &file_ops },
    { 128, &file_ops },
    { 0 },
    { 0 },
    { 0 },
    { 16484, &file_ops },
};
static const int num_drives = (sizeof(drives)/sizeof(*drives));

void disk_drive_init(void) {
    disk_file_init();
}

bool disk_drive_have_boot_disk(void) {
    return disk_file_have_boot_disk();
}

bool disk_drive_read(uint8_t drive, uint8_t track, uint16_t sector, uint8_t *buf, int buf_len) {
    if (num_drives <= drive) {
        return false;
    }
    if (buf_len != SECTOR_SIZE) {
        printf("%s: drive=%u: invalid buf_len (%d)\r\n", __func__, drive, buf_len);
        return false;
    }
    uint32_t lba = track * drives[drive].sectors + sector - 1;
    return drives[drive].ops->read(drive, lba * SECTOR_SIZE, buf, buf_len);
}

bool disk_drive_write(uint8_t drive, uint8_t track, uint16_t sector, uint8_t *buf, int buf_len) {
    if (num_drives <= drive) {
        return false;
    }
    if (buf_len != SECTOR_SIZE) {
        printf("%s: drive=%u: invalid buf_len (%d)\r\n", __func__, drive, buf_len);
        return false;
    }
    uint32_t lba = track * drives[drive].sectors + sector - 1;
    return drives[drive].ops->write(drive, lba * SECTOR_SIZE, buf, buf_len);
}

