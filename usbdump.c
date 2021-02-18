// Skunk USB - drive dumping utility
#include "skunk.h"
#include "usb.h"

static USBDev dev;

void start(void) {
	char buf[2048];
	int i;
	short port = 0;

	skunkRESET();
	skunkNOP();
	skunkNOP();

	inithusb();
	initbulkdev(&dev, port);

	skunkFILEOPEN("usbdump.bin", 0);

	// Dump the first 2MB of the USB drive
	for (i = 0; i < 1024; i++) {
		readblocks(&dev, i * 4, 4, buf);
		skunkFILEWRITE(buf, sizeof(buf));
	}

	skunkFILECLOSE();
	skunkCONSOLECLOSE();
}
