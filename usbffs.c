// Skunk USB - FatFS Filesystem Interaction
#include "skunk.h"
#include "usb.h"
#include <string.h>
#include "sprintf.h"
#include "flash.h"
#include "ffs/ff.h"
#include "ffs/diskio.h"

#define NUM_DEVS 2
static USBDev devs[NUM_DEVS];
static BYTE initialized[NUM_DEVS];
static char cwd[4096];
static char path[4096];
static FATFS fs;
static DIR dir;
static FILINFO fi;
static FIL f;
/* XXX Put this last, or skunkCONSOLEREAD() will clobber cwd now. Why?!? */
static char input[1024];

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
		return "<Unknown FRESULT code>";
	}
#undef CONV
}

static long read_file(void *priv, char *buf, unsigned int bytes)
{
	FIL* fp = priv;
	FRESULT res;

	res = f_read(fp, buf, bytes, &bytes);

	if (res != FR_OK) {
		printf("Error reading from file: %s\n", fresToStr(res));
		return -1;
	}

	/* Temporary: useful to track progress when debugging */
	printf("Read %u of %u bytes from ROM file\n", f_tell(fp), f_size(fp));

	return bytes;
}

static void flash(const char *file) {
	FRESULT res;
	unsigned short erase_blocks = 62; /* Erase entire 4MB bank by default */

	sprintf(path, "%s/%s", cwd, file);

	res = f_open(&f, path, FA_READ);

	if (res != FR_OK) {
		printf("Error opening ROM file '%s': %s\n", path, fresToStr(res));
		return;
	}

	/* Skip ROM header */
	res = f_lseek(&f, 0x2000);

	if (res != FR_OK || f_tell(&f) != 0x2000) {
		printf("Error seeking past ROM header: %s\n", fresToStr(res));
		f_close(&f);
		return;
	}

	if (f_size(&f) <= 0x200000ul) {
		erase_blocks = 30; /* ROM is <= 2MB. Only erase first 2MB */
	}

	flashrom(&read_file, &f, erase_blocks);

	f_close(&f);

	printf("Flashing complete\n");
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

	printf("Starting up\n");

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
		} else if (input[0] == 'f' && input[1] == 'l' &&
				   input[2] == 'a' && input[3] == 's' &&
				   input[4] == 'h') {
			flash(&input[6]);
			f_unmount(DRIVE);
			launchrom();
		} else if (!strcmp("launch", input)) {
			f_unmount(DRIVE);
			launchrom();
		} else if (!strcmp("quit", input)) {
			break;
		} else {
			printf("Invalid command\n");
		}
	}
done:
	f_unmount(DRIVE);
	printf("Exiting\n");
	skunkCONSOLECLOSE();
}
