/* stub/drivers/filesystem.h -- FatFs's calls, on stdio, for shot.c on Linux */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
typedef unsigned UINT;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_EXIST = 8, FR_NO_FILE = 4 } FRESULT;
typedef struct { FILE *fp; } FIL;
typedef struct { int dummy; } FILINFO;
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
extern const char *stub_root;
static inline void stub_path(char *o, const char *p) { snprintf(o, 256, "%s%s", stub_root, p); }
static inline FRESULT f_mkdir(const char *p) { char b[256]; stub_path(b, p); return mkdir(b, 0777) == 0 ? FR_OK : FR_EXIST; }
static inline FRESULT f_stat(const char *p, FILINFO *i) { (void)i; char b[256]; struct stat st; stub_path(b, p); return stat(b, &st) == 0 ? FR_OK : FR_NO_FILE; }
static inline FRESULT f_open(FIL *f, const char *p, int mode) { (void)mode; char b[256]; stub_path(b, p); f->fp = fopen(b, "wb"); return f->fp ? FR_OK : FR_DISK_ERR; }
static inline FRESULT f_write(FIL *f, const void *d, UINT n, UINT *put) { *put = (UINT)fwrite(d, 1, n, f->fp); return FR_OK; }
static inline FRESULT f_close(FIL *f) { return fclose(f->fp) == 0 ? FR_OK : FR_DISK_ERR; }
