#include "microSD.h"

#include "hardware_config.h"
#include "ps1_card_emulator.h"

#include "ff.h"
#include "f_util.h"

#include <stdio.h>

static FATFS fs;

bool load_card_image_from_sd(const char *path) {
    FIL file;
    FILINFO info;
    FRESULT fr;
    UINT read_bytes;

    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    fr = f_stat(path, &info);
    if (fr != FR_OK) {
        printf("f_stat failed: %s %d\n", FRESULT_str(fr), fr);
        f_unmount("0:");
        return false;
    }

    if (info.fsize != PS1_CARD_SIZE) {
        printf("Invalid card image size: %lu\n", (unsigned long)info.fsize);
        f_unmount("0:");
        return false;
    }

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("f_open failed: %s %d\n", FRESULT_str(fr), fr);
        f_unmount("0:");
        return false;
    }

    fr = f_read(&file, card_image, PS1_CARD_SIZE, &read_bytes);
    f_close(&file);

    if (fr != FR_OK || read_bytes != PS1_CARD_SIZE) {
        printf("f_read failed: fr=%d read=%u\n", fr, read_bytes);
        f_unmount("0:");
        return false;
    }

    printf("Card image loaded: %s\n", path);
    return true;
}

bool save_card_image_to_sd(const char *path) {
    FIL file;
    FRESULT fr;
    UINT written;

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("f_open write failed: %d\n", fr);
        return false;
    }

    fr = f_write(&file, card_image, PS1_CARD_SIZE, &written);
    if (fr != FR_OK || written != PS1_CARD_SIZE) {
        printf("f_write failed: fr=%d written=%u\n", fr, written);
        f_close(&file);
        return false;
    }

    fr = f_sync(&file);
    if (fr != FR_OK) {
        printf("f_sync failed: %d\n", fr);
        f_close(&file);
        return false;
    }

    f_close(&file);

    card_dirty = false;
    printf("Card image saved to SD\n");

    return true;
}
