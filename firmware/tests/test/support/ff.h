#ifndef TEST_SUPPORT_FF_H
#define TEST_SUPPORT_FF_H

#include <stdint.h>

typedef uint8_t BYTE;
typedef unsigned int UINT;
typedef uint64_t FSIZE_t;

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
} FRESULT;

typedef struct {
    void *object;
    FSIZE_t position;
    BYTE mode;
} FIL;

typedef struct {
    uint32_t index;
} DIR;

typedef struct {
    FSIZE_t fsize;
    BYTE fattrib;
    char fname[256];
} FILINFO;

typedef struct {
    uint8_t unused;
} FATFS;

#define AM_DIR 0x10u

#define FA_READ          0x01u
#define FA_WRITE         0x02u
#define FA_CREATE_NEW    0x04u
#define FA_OPEN_EXISTING 0x00u

FRESULT f_stat(const char *path, FILINFO *info);
FRESULT f_open(FIL *file, const char *path, BYTE mode);
FRESULT f_read(FIL *file, void *data, UINT count, UINT *read_count);
FRESULT f_write(FIL *file,
                const void *data,
                UINT count,
                UINT *write_count);
FRESULT f_lseek(FIL *file, FSIZE_t offset);
FRESULT f_sync(FIL *file);
FRESULT f_close(FIL *file);
FRESULT f_unlink(const char *path);
FRESULT f_opendir(DIR *dir, const char *path);
FRESULT f_readdir(DIR *dir, FILINFO *info);
FRESULT f_closedir(DIR *dir);

#endif // TEST_SUPPORT_FF_H
