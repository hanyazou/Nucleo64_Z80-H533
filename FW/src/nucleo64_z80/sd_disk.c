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

#include "nucleo64_z80.h"
#include "sd_disk.h"
#include "z80_pins.h"

#include <stdio.h>

struct sd_file_drive sd_file_drives[] = {
    { 26 },
    { 26 },
    { 26 },
    { 26 },
    { 0 },
    { 0 },
    { 0 },
    { 0 },
    { 128 },
    { 128 },
    { 128 },
    { 128 },
    { 0 },
    { 0 },
    { 0 },
    { 16484 },
};
const int sd_file_num_drives = (sizeof(sd_file_drives)/sizeof(*sd_file_drives));

#define NUM_FILES 9
static FIL files[NUM_FILES];
static uint16_t file_available = (1 << NUM_FILES) - 1;

static void sd_disk_select(void);

void sd_disk_init(void)
{
    MX_FATFS_Init();

    struct z80_pin_state pin_state;
    z80_release_pins(&pin_state);
    sd_spi_acquire();

    sd_disk_select();

    sd_spi_release();
    z80_acquire_pins(&pin_state);
}

FIL *sd_disk_get_file(void)
{
    for (int i = 0; i < NUM_FILES; i++) {
        if (file_available & (1L << i)) {
            file_available &= ~(1L << i);
            // printf("%s: allocate %d, available=%04x\n\r", __func__, i, file_available);
            return &files[i];
        }
    }

    return NULL;
}

void sd_disk_put_file(FIL *file)
{
    for (int i = 0; i < NUM_FILES; i++) {
        if (file == &files[i]) {
            assert(!(file_available & (1L << i)));
            file_available |= (1L << i);
            return ;
        }
    }
}

bool sd_disk_have_boot_drive(void)
{
    return (sd_file_drives[0].filep != NULL);
}

static void sd_disk_select(void)
{
    int i;
    unsigned int drive;
    static FATFS FatFs;	//Fatfs handle
    FRESULT fres; //Result after operations
    DIR fsdir;
    FILINFO fileinfo;

    //Open the file system
    fres = f_mount(&FatFs, "", 1); //1=mount now
    if (fres != FR_OK) {
        printf("Failed to mount SD Card (%i)\r\n", fres);
        return;
    }

    //
    // Select disk image folder
    //
    fres = f_opendir(&fsdir, "/");
    if (fres  != FR_OK) {
        printf("Failed to open SD Card (%i)\n\r", fres);
    }

    i = 0;
    int selection = -1;
    int preferred = -1;
    f_rewinddir(&fsdir);
    while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
        if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
            strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
            printf("%d: %s\n\r", i, fileinfo.fname);
            if (strcmp(fileinfo.fname, "CPMDISKS") == 0) {
                selection = i;
            }
            i++;
        }
    }
    if (0 <= preferred) {
        selection = preferred;
    }
    printf("B: ROM BASIC\n\r");
    if (1 < i) {
        if (0 <= selection) {
            printf("Select[%d]: ", selection);
        } else {
            printf("Select: ");
        }
        while (1) {
            uint8_t c = (uint8_t)getchar();  // Wait for input char
            if ('0' <= c && c <= '9' && c - '0' < i) {
                selection = c - '0';
                break;
            }
            if (c == 'b' || c == 'B') {
                printf("B\n\r");
                return;
            }
            if ((c == 0x0d || c == 0x0a) && 0 <= selection)
                break;
        }
        printf("%d\n\r", selection);
        f_rewinddir(&fsdir);
        i = 0;
        while (f_readdir(&fsdir, &fileinfo) == FR_OK && fileinfo.fname[0] != 0) {
            if (strncmp(fileinfo.fname, "CPMDISKS", 8) == 0 ||
                strncmp(fileinfo.fname, "CPMDIS~", 7) == 0) {
                if (selection == i)
                    break;
                i++;
            }
        }
        printf("%s is selected.\n\r", fileinfo.fname);
    } else {
        strcpy(fileinfo.fname, "CPMDISKS");
    }
    f_closedir(&fsdir);

    //
    // Open disk images
    //
    char buf[26];
    for (drive = 0; drive < sd_file_num_drives; drive++) {
        char drive_letter = (char)('A' + drive);
        sprintf(buf, "%s/DRIVE%c.DSK", fileinfo.fname, drive_letter);
        if (f_stat(buf, NULL) != FR_OK) {
            sprintf(buf, "CPMDISKS.CMN/DRIVE%c.DSK", drive_letter);
            if (f_stat(buf, NULL) != FR_OK) {
                continue;
            }
        }
        FIL *filep = sd_disk_get_file();
        if (filep == NULL) {
            printf("Too many files\n\r");
            break;
        }
        if (f_open(filep, buf, FA_READ|FA_WRITE) == FR_OK) {
            printf("Image file %s is assigned to drive %c\n\r", buf, drive_letter);
            sd_file_drives[drive].filep = filep;
        }
    }
    if (!sd_disk_have_boot_drive()) {
        printf("No boot disk.\n\r");
        return;
    }

    return;
}
