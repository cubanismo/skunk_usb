// Skunk USB - FatFS Filesystem Interaction
#include "skunk.h"
#include "usb.h"
#include <string.h>
#include "sprintf.h"
#include "ffs/ff.h"
#include "ffs/diskio.h"

#define NUM_DEVS 2
static USBDev devs[NUM_DEVS];
static BYTE initialized[NUM_DEVS];
static char input[1024];
static char cwd[4096];
static char path[4096];
static FATFS fs;
static DIR dir;
static FILINFO fi;

DSTATUS disk_initialize(BYTE pdrv)
{
	const short port = pdrv;

	if (pdrv >= NUM_DEVS) {
		return STA_NODISK;
	}

	inithusb();
	initbulkdev(&devs[pdrv], port);

	initialized[pdrv] = 1;

	return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
	if (pdrv >= NUM_DEVS) {
		return STA_NODISK;
	}

	if (!initialized[pdrv]) {
		return STA_NOINIT;
	}

	return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv >= NUM_DEVS) {
		return RES_PARERR;
	}

	if (!initialized[pdrv]) {
		return RES_NOTRDY;
	}

	readblocks(&devs[pdrv], sector, count, buff);

	return RES_OK;
}

static const char *fresToStr(FRESULT fr) {

#define CONV(a) case a: return #a
	switch (fr) {
		CONV(FR_OK);
		CONV(FR_DISK_ERR);
		CONV(FR_INT_ERR);
		CONV(FR_NOT_READY);
		CONV(FR_NO_FILE);
		CONV(FR_NO_PATH);
		CONV(FR_INVALID_NAME);
		CONV(FR_DENIED);
		CONV(FR_EXIST);
		CONV(FR_INVALID_OBJECT);
		CONV(FR_WRITE_PROTECTED);
		CONV(FR_INVALID_DRIVE);
		CONV(FR_NOT_ENABLED);
		CONV(FR_NO_FILESYSTEM);
		CONV(FR_MKFS_ABORTED);
		CONV(FR_TIMEOUT);
		CONV(FR_LOCKED);
		CONV(FR_NOT_ENOUGH_CORE);
		CONV(FR_TOO_MANY_OPEN_FILES);
		CONV(FR_INVALID_PARAMETER);
	default:
		return "<Unkown FRESULT code>";
	}
#undef CONV
}

static void ls(void) {
	FRESULT res;

	res = f_opendir(&dir, cwd);
	if (res != FR_OK) {
		printf("Failed open dir: %s\n", fresToStr(res));
		return;
	}

	while (1) {
		res = f_readdir(&dir, &fi);
		if (res != FR_OK) {
			printf("Failed to read next file in dir: %s\n", fresToStr(res));
			goto done;
		}

		if (fi.fname[0] == 0) {
			break;
		}

		printf("%s%s\n", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "");
	}

done:
	res = f_closedir(&dir);
	if (res != FR_OK) {
		printf("Failed to close dir: %s\n", fresToStr(res));
		goto done;
	}
}

static void pwd(void) {
	printf("%s\n", cwd);
}

static int cdup(void) {
	int i;
	int lastslash = 0;
	int numslash = 0;

	for (i = 0; cwd[i]; i++) {
		if (cwd[i] == '/') {
			lastslash = i;
			numslash++;
		}
		path[i] = cwd[i];
	}
	path[i] = '\0';

	if (numslash > 1) {
		path[lastslash] = '\0';
		return lastslash;
	}

	return i;
}

static void cd(const char *newdir) {
	FRESULT res;
	int chars;

	if (!strcmp(newdir, "..")) {
		chars = cdup();
	} else {
		chars = sprintf(path, "%s/%s", cwd, newdir);
	}

	res = f_opendir(&dir, path);
	if (res != FR_OK) {
		printf("Error changing directory to %s: %s\n", newdir, fresToStr(res));
		return;
	}

	f_closedir(&dir);
	memcpy(cwd, path, chars + 1);
	pwd();
}

#define DRIVE "0"

void start(void) {
	FRESULT res;
	int i;

	for (i = 0; i < NUM_DEVS; i++) {
		initialized[i] = 0;
	}

	skunkRESET();
	skunkNOP();
	skunkNOP();

	res = f_mount(&fs, DRIVE, 1);
	if (res != FR_OK) {
		printf("Failed mounting FS: %s\n", fresToStr(res));
		goto done;
	}

	sprintf(cwd, "0:/");

	ls();

	while (1) {
		memset(input, 0, sizeof(input));
		skunkCONSOLEREAD(input, sizeof(input) - 1);

		if (!strcmp("ls", input) || !strcmp("dir", input)) {
			ls();
		} else if (!strcmp("pwd", input)) {
			pwd();
		} else if (input[0] == 'c' && input[1] == 'd') {
			cd(&input[3]);
		} else {
			skunkCONSOLEWRITE("Invalid command\n");
		}
	}
done:
	f_unmount(DRIVE);
	skunkCONSOLECLOSE();
}
