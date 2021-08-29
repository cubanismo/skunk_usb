// Skunk USB - FatFS Filesystem Interaction
#include "skunk.h"
#include "usb.h"
#include "sprintf.h"
#include "ffs/ff.h"
#include "ffs/diskio.h"

#define NUM_DEVS 2
static USBDev devs[NUM_DEVS];
static BYTE initialized[NUM_DEVS];

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

void start(void) {
	FRESULT res;
	FATFS fs;
	DIR dir;
	FILINFO fi;
	int i;

	for (i = 0; i < NUM_DEVS; i++) {
		initialized[i] = 0;
	}

	skunkRESET();
	skunkNOP();
	skunkNOP();

	res = f_mount(&fs, "0", 1);
	if (res != FR_OK) {
		printf("Failed mounting FS: %s\n", fresToStr(res));
		goto done;
	}

	res = f_opendir(&dir, "0:/");
	if (res != FR_OK) {
		printf("Failed open dir: %s\n", fresToStr(res));
		goto done;
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

	skunkCONSOLEWRITE("Read all files in root directory\n");

	res = f_closedir(&dir);
	if (res != FR_OK) {
		printf("Failed to close dir: %s\n", fresToStr(res));
		goto done;
	}

done:
	skunkCONSOLECLOSE();
}
