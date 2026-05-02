#include <stdio.h>
#include <string.h>
#include "microsd.h"
#include "ff.h"
#include "f_util.h"
#include "hardware_config.h"
#include "ps1_card.h"


FATFS fs;

bool write_test_file(void){
    FIL file;
    FRESULT fr;
    UINT written;
    
    const char *text = 
        "TEST TEXT";
    
    fr = f_open(&file, "test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    if(FR_OK != fr){
        printf("f_open failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    fr = f_write(&file, text, strlen(text), &written);
    if(FR_OK != fr){
        printf("f_write failed: %s %d\n", FRESULT_str(fr), fr);
        f_close(&file);
        return false;
    }

    printf("Written bytes: %u\n", written);

    fr = f_close(&file);
    if(FR_OK != fr){
        printf("f_close failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    return true;
}

bool read_test_file(void){
    FIL file;
    FRESULT fr;
    char buffer[64];
    UINT read_bytes;


    fr = f_open(&file, "test.txt", FA_READ);
    if(FR_OK != fr){
        printf("f_open failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    printf("Reading test.txt\n");

    do{
        memset(buffer, 0, sizeof(buffer));

        fr = f_read(&file, buffer, sizeof(buffer) - 1, &read_bytes);
        if (fr != FR_OK) {
            printf("f_read failed: %s %d\n", FRESULT_str(fr), fr);
            f_close(&file);
            return false;
        }

        if (read_bytes > 0) {
            printf("%s", buffer);
        }
    }while(read_bytes>0);

    printf("\nRead finished\n");

    f_close(&file);
    if(FR_OK != fr){
        printf("f_close failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }
    return true;
}

void test_microsd(void)
{
    printf("PS memory card - micro sd input/output test\n");
    FRESULT fr = f_mount(&fs, "0:", 1);
    if(FR_OK != fr){
        printf("f_mount failed: %s %d\n", FRESULT_str(fr), fr);
    }
    
    printf("microSD mounted OK\n");

    if (!write_test_file()) {
        printf("Write test FAILED\n");
    } else {
        printf("Write test OK\n");
    }

    if (!read_test_file()) {
        printf("Read test FAILED\n");
    } else {
        printf("Read test OK\n");
    }
    f_unmount("0:");
}

bool test_backup_ps1_card_to_microsd(const char *path){
    FIL file;
    FRESULT fr;
    UINT written;
    uint8_t frame[PS1_FRAME_SIZE];

    printf("Mounting microSD...\n");
    fr = f_mount(&fs, "0:", 1);
    if (FR_OK != fr){
        printf("f_mount failed: %d\n", fr);
        return false;
    }

    printf("microSD mounted OK\n");
    
    printf("Creating image file: %s\n", path);
    
    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr){
        printf("f_open failed: %d\n", fr);
        return false;
    }

    for (uint16_t frame_no = 0; frame_no < PS1_FRAME_COUNT; frame_no++){
        bool ok = ps1_mc_read_frame(frame_no, frame);

        if(!ok){
            printf("PS1 read failed at frame %u\n", frame_no);
            f_close(&file);
            f_unmount("0:");
            return false;
        }

        fr = f_write(&file, frame, PS1_FRAME_SIZE, &written);

        if(FR_OK != fr){
            printf("f_write failed at %u, fr=%d, written=%u\n", frame_no, fr, written);
            f_close(&file);
            f_unmount("0:");
            return false;
        }

        if((frame_no % 32) == 0){
            printf("Backed up progress %u / %u\n", frame_no, PS1_FRAME_COUNT);
        }
    }
    fr = f_sync(&file);
    if(FR_OK != fr){
        printf("f_sync failed: %d\n", fr);
        f_close(&file);
        f_unmount("0:");
        return false;
    }

    fr = f_close(&file);
    if(FR_OK != fr){
        printf("f_close failed: %d\n", fr);
        f_unmount("0:");
        return false;
    }
    printf("Backup complete\n");
    printf("Expected image size: %u bytes\n", PS1_CARD_SIZE);
    f_unmount("0:");

    return true;
}