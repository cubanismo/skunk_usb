// Skunk USB - verify host USB functionality
#include "skunk.h"
#include "usb.h"

static USBDev dev0, dev1;

void start(void) {
	char buf[2048];
	int i;

	skunkRESET();
	skunkNOP();
	skunkNOP();

	inithusb();
	initbulkdev(&dev0, 0);
	initbulkdev(&dev1, 1);

	skunkFILEOPEN("rand0.bin", 1);

	// Write 2MB of rand0.bin to the USB drive on port 0
	for (i = 0; i < 1024; i++) {
		skunkFILEREAD(buf, sizeof(buf));
		writeblocks(&dev0, i * 4, 4, buf);
	}

	skunkFILECLOSE();

	skunkFILEOPEN("rand1.bin", 1);

	// Write 2MB of rand1.bin to the USB drive on port 1
	for (i = 0; i < 1024; i++) {
		skunkFILEREAD(buf, sizeof(buf));
		writeblocks(&dev1, i * 4, 4, buf);
	}

	skunkFILECLOSE();

	skunkFILEOPEN("usbdump0.bin", 0);

	// Dump the 2MB written to the USB drive on port 0 back to usbdump0.bin
	for (i = 0; i < 1024; i++) {
		readblocks(&dev0, i * 4, 4, buf);
		skunkFILEWRITE(buf, sizeof(buf));
	}

	skunkFILECLOSE();

	// XXX I don't know why this is needed, but the following skunkFILEOPEN()
	// crashes the Jaguar without it.
	skunkCONSOLEWRITE("WAR crash opening second file to write\r\n");

	skunkFILEOPEN("usbdump1.bin", 0);

	// Dump the 2MB written to the USB drive on port 1 back to usbdump1.bin
	for (i = 0; i < 1024; i++) {
		readblocks(&dev1, i * 4, 4, buf);
		skunkFILEWRITE(buf, sizeof(buf));
	}

	skunkFILECLOSE();
	skunkCONSOLECLOSE();
}
