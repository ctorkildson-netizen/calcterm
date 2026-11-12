/* stub/drivers/filesystem.h -- FatFs's calls, on stdio, for shot.c and
 * xfer.c on Linux: paths are under stub_root. */
#pragma once
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
typedef unsigned UINT;
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_NO_FILE = 4, FR_NO_PATH = 5, FR_EXIST = 8 } FRESULT;
typedef struct { FILE *fp; } FIL;
typedef struct { DIR *d; char path[256]; } DIR_;
#define DIR DIR_
typedef struct { uint64_t fsize; uint16_t fdate, ftime; uint8_t fattrib; char fname[256]; } FILINFO;
#define FA_READ 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_DIR 0x10
#define FS_MAX_PATH_LEN (260)
extern const char *stub_root;
static inline void stub_path(char *o, const char *p) { snprintf(o, 256, "%s%s", stub_root, p); }
static inline void stub_info(FILINFO *i, const char *full, const char *name)
{
	struct stat st;
	memset(i, 0, sizeof *i);
	if (stat(full, &st)) return;
	snprintf(i->fname, sizeof i->fname, "%s", name);
	i->fsize = (uint64_t)st.st_size;
	i->fattrib = S_ISDIR(st.st_mode) ? AM_DIR : 0;
	struct tm tm;
	localtime_r(&st.st_mtime, &tm);
	i->fdate = (uint16_t)((tm.tm_year - 80) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
	i->ftime = (uint16_t)(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
}
static inline FRESULT f_mkdir(const char *p) { char b[256]; stub_path(b, p); return mkdir(b, 0777) == 0 ? FR_OK : FR_EXIST; }
static inline FRESULT f_stat(const char *p, FILINFO *i)
{
	char b[256]; struct stat st; stub_path(b, p);
	if (stat(b, &st) != 0) return FR_NO_FILE;
	if (i) { const char *n = strrchr(p, '/'); stub_info(i, b, n ? n + 1 : p); }
	return FR_OK;
}
static inline FRESULT f_open(FIL *f, const char *p, int mode)
{
	char b[256]; stub_path(b, p);
	f->fp = fopen(b, mode & FA_READ ? "rb" : "wb");
	return f->fp ? FR_OK : FR_NO_FILE;
}
static inline FRESULT f_write(FIL *f, const void *d, UINT n, UINT *put) { *put = (UINT)fwrite(d, 1, n, f->fp); return FR_OK; }
static inline FRESULT f_read(FIL *f, void *d, UINT n, UINT *got) { *got = (UINT)fread(d, 1, n, f->fp); return FR_OK; }
static inline FRESULT f_lseek(FIL *f, uint64_t at) { return fseek(f->fp, (long)at, SEEK_SET) ? FR_DISK_ERR : FR_OK; }
static inline uint64_t f_tell(FIL *f) { return (uint64_t)ftell(f->fp); }
static inline FRESULT f_close(FIL *f) { return fclose(f->fp) == 0 ? FR_OK : FR_DISK_ERR; }
static inline FRESULT f_unlink(const char *p) { char b[256]; stub_path(b, p); return unlink(b) == 0 ? FR_OK : FR_NO_FILE; }
static inline FRESULT f_opendir(DIR *d, const char *p)
{
	stub_path(d->path, p);
	d->d = opendir(d->path);
	return d->d ? FR_OK : FR_NO_PATH;
}
static inline FRESULT f_readdir(DIR *d, FILINFO *i)
{
	struct dirent *e;
	do e = readdir(d->d); while (e && (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0));
	if (!e) { i->fname[0] = 0; return FR_OK; }
	char full[600];
	snprintf(full, sizeof full, "%s/%s", d->path, e->d_name);
	stub_info(i, full, e->d_name);
	return FR_OK;
}
static inline FRESULT f_closedir(DIR *d) { closedir(d->d); return FR_OK; }
