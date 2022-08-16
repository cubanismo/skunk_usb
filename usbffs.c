// Skunk USB - FatFS Filesystem Interaction
#if defined(USE_SKUNK)
#include "skunk.h"
#endif
#include "usb.h"
#include <string.h>
#include "sprintf.h"
#include "flash.h"
#include "ffsgpu.h"
#include "dspjoy.h"
#include "ffs/ff.h"
#include "ffs/diskio.h"

#define CHECKEDPF(...) if (!consoleBusy) printf(__VA_ARGS__)

extern unsigned long testgpu(void);
extern void showgl(int show);
extern volatile unsigned short doscale;
extern volatile unsigned long ticks;

#define AM_SKUNKUSB_CDUP 0x80 /* Hacky, but max used by FFS is 0x20 */
#define MAX_DISPLAYED_GAMES (GL_HEIGHT / FNTHEIGHT)
#define CDUP_STRING "<Parent Directory>"

/*
 * Note non-const global initializers don't work. We could clear BSS to
 * zero-initialize everything, but the C runtime startup code that would
 * initialize global variables isn't used by default, so globals must be
 * initialized at runtime. The same likely applies to static variables in
 * functions.
 */

#define NUM_DEVS 2
static USBDev devs[NUM_DEVS];
static BYTE initialized[NUM_DEVS];
static char cwd[4096];
static char path[4096];
static char input[1024];
static FATFS fs;
static DIR dir;
static FILINFO fi[1024];
static unsigned short numFiles;
static short scrollStart;
static BYTE consoleBusy;

struct FlashData {
	FIL f;
	unsigned int bytesPerLine;
	unsigned short totalBlocks;
	unsigned short erasedBlocks;
	unsigned short linesDrawn;
};

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
	struct FlashData *data = priv;
	int bytesRead;
	unsigned short linesRead;
	FRESULT res;

	if (buf) {
		res = f_read(&data->f, buf, bytes, &bytes);

		if (res != FR_OK) {
			CHECKEDPF("Error reading from file: %s\n", fresToStr(res));
			return -1;
		}

		bytesRead = f_tell(&data->f) - 0x2000;

		linesRead = bytesRead / data->bytesPerLine;

		CHECKEDPF("Lines read: %u Lines drawn: %u\n",
				  linesRead, data->linesDrawn);

		if (linesRead > data->linesDrawn) {
			invertrect(gamelstbm,
					   ((data->totalBlocks + data->linesDrawn) << 16) | 0,
					   ((linesRead - data->linesDrawn) << 16 | GL_WIDTH));
			data->linesDrawn = linesRead;
		}

		CHECKEDPF("Read %u of %u bytes from ROM file\n",
				  bytesRead, f_size(&data->f) - 0x2000);
	} else {
		/* A NULL buffer means this is a notification a block has been erased */
		data->erasedBlocks++;
		CHECKEDPF("Erased %u of %u blocks\n",
				  data->erasedBlocks, data->totalBlocks);
		invertrect(gamelstbm,
				   ((data->erasedBlocks-1) << 16) | 0,
				   (1 << 16) | GL_WIDTH);
	}

	return bytes;
}

static unsigned long flash(const char *file) {
	FRESULT res;
	struct FlashData data;
	unsigned long startAddr;
	unsigned short flashLines;
	unsigned int bytesRead;

	data.totalBlocks = 70; /* Erase entire 4MB bank by default */
	data.erasedBlocks = 0;
	data.linesDrawn = 0;

	sprintf(path, "%s/%s", cwd, file);

	res = f_open(&data.f, path, FA_READ);

	if (res != FR_OK) {
		CHECKEDPF("Error opening ROM file '%s': %s\n", path, fresToStr(res));
		return 0;
	}

	/* Skip to start address in ROM header  */
	res = f_lseek(&data.f, 0x404);

	if (res != FR_OK || f_tell(&data.f) != 0x404) {
		CHECKEDPF("Error seeking to start address in ROM header: %s\n",
				  fresToStr(res));
		f_close(&data.f);
		return 0;
	}

	res = f_read(&data.f, &startAddr, sizeof(startAddr), &bytesRead);

	if (res != FR_OK || bytesRead != sizeof(startAddr)) {
		CHECKEDPF("Error reading ROM start address: %s\n", fresToStr(res));
		f_close(&data.f);
		return 0;
	}

	CHECKEDPF("ROM start address from header: 0x%08lx\n", startAddr);

	/* Skip ROM header */
	res = f_lseek(&data.f, 0x2000);

	if (res != FR_OK || f_tell(&data.f) != 0x2000) {
		CHECKEDPF("Error seeking past ROM header: %s\n", fresToStr(res));
		f_close(&data.f);
		return 0;
	}

	if (f_size(&data.f) <= 0x200000ul) {
		data.totalBlocks = 38; /* ROM is <= 2MB. Only erase first 2MB */
	}

	flashLines = GL_HEIGHT - data.totalBlocks;
	data.bytesPerLine = (f_size(&data.f) - 0x2000) / flashLines;
	CHECKEDPF("flashLines: %u bytesPerLine: %u\n",
			  flashLines, data.bytesPerLine);

	/* Clear the game list so we can use it as a status bar */
	clrgamelst();

	/* 8 initial blocks are always used */
	flashrom(&read_file, &data, data.totalBlocks - 8);

	f_close(&data.f);

	CHECKEDPF("Flashing complete\n");

	return startAddr;
}

enum {
	SELECT_CLR		= 0x0001,
	SELECT_DRAW		= 0x0002,
	SELECT_ADD		= 0x0004,
	SELECT_SET		= 0x0008,
	SELECT_VAL_MASK	= SELECT_ADD | SELECT_SET,
} SelectOps;

static void resetscroll(void)
{
	scrollStart = 0;
}

static void scroll(short selection)
{
	unsigned coord = 0;
	short needDraw = 0;
	short idx;

	if (selection < scrollStart)
	{
		scrollStart = selection;
		needDraw = 1;
	}

	if (selection >= (scrollStart + MAX_DISPLAYED_GAMES)) {
		scrollStart = (selection - MAX_DISPLAYED_GAMES) + 1;
		needDraw = 1;
	}

	if (!needDraw)
		return;

	clrgamelst();
	for (idx = scrollStart; idx < (scrollStart + MAX_DISPLAYED_GAMES); idx++) {
		if (idx >= numFiles) break;

		sprintf(input, "%s%s",
				fi[idx].fname, (fi[idx].fattrib & AM_DIR) ? "/" : "");
		drawstring(gamelstbm, coord, input);
		coord += (FNTHEIGHT << 16);
	}
}

static short select(unsigned short op, short val) {
	static short selection = -1;
	short newSelection = selection;
	if ((selection >= 0) && (op & SELECT_CLR)) {
		invertrect(gamelstbm,
				   (((selection - scrollStart)* FNTHEIGHT) << 16) | 0,
				   (FNTHEIGHT << 16) | GL_WIDTH);
	}

	switch (op & SELECT_VAL_MASK) {
	case SELECT_ADD:
		newSelection += val;
		break;
	case SELECT_SET:
		newSelection = val;
		break;

	default:
		CHECKEDPF("Invalid selection val operation: 0x%x\n",
				  op & SELECT_VAL_MASK);
		/* Fall through */
	case 0:
		break;
	}

	if (newSelection < 0) {
		newSelection = 0;
	}

	if (newSelection >= numFiles) {
		newSelection = numFiles - 1;
	}

	selection = newSelection;

	if ((selection >= 0) && (op & SELECT_DRAW)) {
		scroll(selection);
		invertrect(gamelstbm,
				   (((selection - scrollStart) * FNTHEIGHT) << 16) | 0,
				   (FNTHEIGHT << 16) | GL_WIDTH);
	}

	return selection;
}

static short attopdir(void)
{
	int numslash = 0;
	int i;

	for (i = 0; cwd[i]; i++) {
		if (cwd[i] == '/') {
			if (++numslash > 1) {
				return 0;
			}
		}
	}

	return 1;
}

static void ls(void) {
	unsigned coord = 0;
	unsigned idx = 0;
	FRESULT res;

	clrgamelst();

	res = f_opendir(&dir, cwd);
	if (res != FR_OK) {
		CHECKEDPF("Failed open dir: %s\n", fresToStr(res));
		return;
	}

	resetscroll();

	idx = 0;
	if (!attopdir()) {
		/* Insert a fake file that moves up a directory */
		sprintf(fi[idx].fname, "%s", CDUP_STRING);
		fi[idx].fattrib = AM_DIR | AM_SKUNKUSB_CDUP;

		drawstring(gamelstbm, coord, fi[idx].fname);
		coord += (FNTHEIGHT << 16);

		idx++;
	}

	for (; 1; idx++) {
		res = f_readdir(&dir, &fi[idx]);
		if (res != FR_OK) {
			CHECKEDPF("Failed to read next file in dir: %s\n", fresToStr(res));
			goto done;
		}

		if (fi[idx].fname[0] == 0) {
			break;
		}

		sprintf(input, "%s%s",
				fi[idx].fname,
				((fi[idx].fattrib & (AM_DIR | AM_SKUNKUSB_CDUP)) == AM_DIR) ?
				"/" : "");

		if (idx < MAX_DISPLAYED_GAMES) {
			drawstring(gamelstbm, coord, input);
			coord += (FNTHEIGHT << 16);
		}

		CHECKEDPF("%s\n", input);
	}

	numFiles = idx;

done:
	res = f_closedir(&dir);
	if (res != FR_OK) {
		CHECKEDPF("Failed to close dir: %s\n", fresToStr(res));
	}

	select(SELECT_DRAW | SELECT_SET, 0);
}

static void pwd(void) {
	CHECKEDPF("%s\n", cwd);
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
		CHECKEDPF("Error changing directory to %s: %s\n",
				  newdir, fresToStr(res));
		return;
	}

	f_closedir(&dir);
	memcpy(cwd, path, chars + 1);
}

#define DRIVE "0"

static void dorom(const char *fname) {
	unsigned long startAddr = 0x802000;

	CHECKEDPF("Stopping DSP\n");
	stopdsp();
	if (fname) {
		CHECKEDPF("Flashing\n");
		startAddr = flash(fname);
	}
	CHECKEDPF("Unmounting drive\n");
	f_unmount(DRIVE);
	CHECKEDPF("Stopping GPU\n");
	stopgpu();
	CHECKEDPF("Launching\n");
#if defined(USE_SKUNK)
	if (!consoleBusy) {
		skunkCONSOLECLOSE();
	}
#endif /* defined(USE_SKUNK) */
	launchrom(startAddr);
}

void start(void) {
	FRESULT res;
	int i;
	static int showList = 1;

	for (i = 0; i < NUM_DEVS; i++) {
		initialized[i] = 0;
	}

	consoleBusy = 0;
	numFiles = 0;

#if defined(USE_SKUNK)
	skunkRESET();
	skunkNOP();
	skunkNOP();
#endif /* defined(USE_SKUNK) */

	printf("Starting up\n");

	startdsp();

	res = f_mount(&fs, DRIVE, 1);
	if (res != FR_OK) {
		printf("Failed mounting FS: %s\n", fresToStr(res));
		goto done;
	}

	sprintf(cwd, "0:/");

	showgl(1);
	ls();

	while (1) {
#if defined(USE_SKUNK)
		if (!skunkCONSOLESETUPREAD()) break;
		consoleBusy = 1;
		memset(input, 0, sizeof(input));
		while (!skunkCONSOLECHECKREAD())
#endif
		{
			for (; joyevget != joyevput; joyevget = (joyevget + 8) % 1024) {
				unsigned short i = joyevget >> 2;
				unsigned long ev = joyevbuf[i];
				short idx;

				if (ev & (1 << 12)) {
					switch (ev & 0xff) {
					case JB_UP:
						select(SELECT_CLR|SELECT_DRAW|SELECT_ADD, -1);
						break;
					case JB_DOWN:
						select(SELECT_CLR|SELECT_DRAW|SELECT_ADD, 1);
						break;
					case JB_B:
						idx = select(0, 0);
						if (idx >= 0) {
							if (fi[idx].fattrib & AM_DIR) {
								if (fi[idx].fattrib & AM_SKUNKUSB_CDUP) {
									cd("..");
								} else {
									cd(fi[idx].fname);
								}
								ls();
							} else {
								dorom(fi[idx].fname /* Flash ROM and launch */);
							}
						}
						break;
					case JB_C:
						dorom(NULL /* Don't flash anything. Just launch */);
						break;
					case JB_OPTION:
						ls();
						break;
					case JB_PAUSE:
						showgl(showList = !showList);
						break;
					default:
						break;
					}
				}
			}
		}
#if defined(USE_SKUNK)
		skunkCONSOLEFINISHREAD(input, sizeof(input) - 1);
		consoleBusy = 0;

		if (!strcmp("ls", input) || !strcmp("dir", input)) {
			ls();
		} else if (!strcmp("pwd", input)) {
			pwd();
		} else if (input[0] == 'c' && input[1] == 'd') {
			cd(&input[3]);
			pwd();
		} else if (input[0] == 'f' && input[1] == 'l' &&
				   input[2] == 'a' && input[3] == 's' &&
				   input[4] == 'h') {
			dorom(&input[6] /* Flash ROM and launch  */);
		} else if (!strcmp("launch", input)) {
			dorom(NULL /* Don't flash, just launch */);
		} else if (!strcmp("quit", input)) {
			break;
		} else if (!strcmp("testgpu", input)) {
			printf("gpusem = %u\n", testgpu());
		} else if (!strcmp("togglescale", input)) {
			doscale = !doscale;
		} else if (!strcmp("getticks", input)) {
			printf("ticks = %u\n", ticks);
		} else if (!strcmp("showlist", input)) {
			showgl(1);
		} else if (!strcmp("hidelist", input)) {
			showgl(0);
		} else if (!strcmp("clearlist", input)) {
			clrgamelst();
		} else if (!strcmp("drawstring", input)) {
			drawstring(gamelstbm, (0 << 16) | 0, "Hello you big, beautiful world!");
			drawstring(gamelstbm, (FNTHEIGHT << 16) | 0, "12345678901234567890");
		} else if (!strcmp("next", input)) {
			select(SELECT_CLR|SELECT_DRAW|SELECT_ADD, 1);
		} else if (!strcmp("prev", input)) {
			select(SELECT_CLR|SELECT_DRAW|SELECT_ADD, -1);
		} else if (!strcmp("dumpbuts", input)) {
#define PRINTBUTTONONPORT(PORT, BUTTON) \
	if (butsmem##PORT & JB_MASK(BUTTON)) \
		printf("Joypad " #PORT ": '" #BUTTON "' pressed\n")

#define PRINTBUTTON(BUTTON) \
	PRINTBUTTONONPORT(0, BUTTON); \
	PRINTBUTTONONPORT(1, BUTTON)

			printf("Joypad 0 raw buttons: 0x%08x\n", butsmem0);
			printf("Joypad 1 raw buttons: 0x%08x\n", butsmem1);
			printf("joyevget: %u joyevput: %u\n", joyevget, joyevput);
			for (; joyevget != joyevput; joyevget = (joyevget + 8) % 1024) {
				unsigned short i = joyevget >> 2;
				printf("  Event button: %u %s at %u\n",
					   (joyevbuf[i] & 0xff),
					   (joyevbuf[i] & (1<<12)) ? "Down" : " Up ",
					   joyevbuf[i+1]);
			}
			PRINTBUTTON(0);
			PRINTBUTTON(1);
			PRINTBUTTON(2);
			PRINTBUTTON(3);
			PRINTBUTTON(4);
			PRINTBUTTON(5);
			PRINTBUTTON(6);
			PRINTBUTTON(7);
			PRINTBUTTON(8);
			PRINTBUTTON(9);
			PRINTBUTTON(STAR);
			PRINTBUTTON(HASH);
			PRINTBUTTON(A);
			PRINTBUTTON(B);
			PRINTBUTTON(C);
			PRINTBUTTON(OPTION);
			PRINTBUTTON(PAUSE);
			PRINTBUTTON(UP);
			PRINTBUTTON(DOWN);
			PRINTBUTTON(LEFT);
			PRINTBUTTON(RIGHT);
		} else {
			printf("Invalid command\n");
		}
#endif /* defined(USE_SKUNK) */
	}
done:
	f_unmount(DRIVE);
	stopdsp();
	stopgpu();
	printf("Exiting\n");
#if defined(USE_SKUNK)
	skunkCONSOLECLOSE();
#endif /* defined(USE_SKUNK) */
}
