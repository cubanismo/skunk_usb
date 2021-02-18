// Test the Skunk USB mass storage driver
#include "skunk.h"
#include "usb.h"

extern int printf(const char *fmt, ...);
extern int sprintf(char *str, const char *fmt, ...);

static USBDev dev;

static void xxdbuf(const char *buf, int len)
{
	char str[100];
	char *sPtr;
	int i, j;
	unsigned int d;
	char c;

	for (j = 0; j < ((len + 15) / 16); j++) {
		// Build up the line in a local string buffer. printf()ing one or two
		// chars at a time over the skunk connection to the host is too slow.
		sPtr = str + sprintf(str, "%08x: ", j * 16);
		for (i = 0; i < 16; i += 2) {
			if (((j * 16) + i) >= 0x200)
				break;

			d = (*(unsigned short *)&buf[(j * 16) + i]) & 0xffff;
			sPtr += sprintf(sPtr, "%04x ", d);
		}

		sPtr += sprintf(sPtr, " ");

		for (i = 0; i < 16; i++) {
			if (((j * 16) + i) >= 0x200)
				break;

			c = buf[(j * 16) + i];

			if (c < 0x20 || c > 0x7e) {
				sPtr += sprintf(sPtr, ".");
			} else {
				sPtr += sprintf(sPtr, "%c", c);
			}
		}

		printf("%s\r\n", str);
	}
}

void fillbuf(char *buf, int len)
{
	int i, val = 0;

	for (i = 0; i < len; i++) {
		buf[i] = val++;
		if (val > 0xff) val = 0;
	}
}

void start() {
	char inbuf[0x200];
	char outbuf[0x200];
	char checkbuf[0x200];

	short port = 0;

	skunkRESET();
	skunkNOP();
	skunkNOP();

	printf("Starting up\r\n\r\n");

	inithusb();
	initbulkdev(&dev, port);

	// Time to get down to it. Read in the first logical block of data:
	readblocks(&dev, 0, 1, inbuf);

	// Print out the data in "xxd" format
	printf("Raw data from logical block 0:\r\n");
	xxdbuf(inbuf, sizeof(inbuf));

	// Now write some data over the first block
	fillbuf(outbuf, sizeof(outbuf));
	writeblocks(&dev, 0, 1, outbuf);

	// Read back what we wrote to check it
	readblocks(&dev, 0, 1, checkbuf);
	printf("\r\nData written to logical block 0:\r\n");
	printf("(Should be 0x00, 0x01, ... 0xfe, 0xff, 0x00, 0x01 ...)\r\n");
	xxdbuf(checkbuf, sizeof(checkbuf));

	// Restore the original data
	writeblocks(&dev, 0, 1, inbuf);

	printf("\r\nDone!\r\n");

	skunkCONSOLECLOSE();

	// There's nowhere for us to return!
	while (1);
}
