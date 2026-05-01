#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ff.h"
#include "f_util.h"

FATFS fs;

bool write_test_file(void){
    FIL file;
    FRESULT fr;
    UINT written;
    
    const char *text = 
        "Test test test";
    
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

void test_write_read_microsd(void)
{
    stdio_init_all();
    
    printf("PS memory card - micro sd test\n");
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