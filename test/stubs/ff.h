// Host-test stub for ChaN's FatFs, as vendored by no-OS-FatFS-SD-SDIO-SPI.
//
// Enough of the API for sd_log.cpp to typecheck. Does no I/O: the SD path is
// exercised on hardware, not here. What this catches is the thing that is
// otherwise only found on a device -- a renamed call or a wrong argument order.
#pragma once
#include <stdint.h>

typedef unsigned int UINT;
typedef uint32_t FSIZE_t;

typedef enum {
	FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE,
	FR_NO_PATH, FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT,
	FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM,
} FRESULT;

#define FA_READ          0x01
#define FA_WRITE         0x02
#define FA_CREATE_NEW    0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS   0x10

typedef struct { int dummy; } FATFS;
typedef struct { int dummy; } FIL;
typedef struct { FSIZE_t fsize; } FILINFO;

FRESULT f_mount(FATFS *fs, const char *path, uint8_t opt);
FRESULT f_unmount(const char *path);
FRESULT f_open(FIL *fp, const char *path, uint8_t mode);
FRESULT f_close(FIL *fp);
FRESULT f_write(FIL *fp, const void *buf, UINT btw, UINT *bw);
FRESULT f_sync(FIL *fp);
FRESULT f_truncate(FIL *fp);
FRESULT f_expand(FIL *fp, FSIZE_t fsz, uint8_t opt);
FRESULT f_stat(const char *path, FILINFO *fno);
