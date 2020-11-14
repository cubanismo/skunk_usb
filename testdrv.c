// Test the Skunk USB mass storage driver
#include "skunk.h"
#include "usb.h"

extern int printf(const char *fmt, ...);
extern int sprintf(char *str, const char *fmt, ...);

static USBDev dev;

void start() {
	char buf[2048];
	char str[100];
	char *sPtr;
	int i, j;
	unsigned int d;
	char c;
	short port = 1;

	skunkRESET();
	skunkNOP();
	skunkNOP();

	printf("Staring up\r\n\r\n");

	initbulkdev(&dev, port);

	// Time to get down to it. Read in the first logical block of data:
	readblocks(&dev, 0, 1, buf);

	// Print out the data in "xxd" format
	printf("Raw data from logical block 0:\r\n");
	for (j = 0; j < ((0x200 + 15) / 16); j++) {
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

	printf("\r\nDone!\r\n");
	// There's nowhere for us to return!
	while (1);
}
