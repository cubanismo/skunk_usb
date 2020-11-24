#ifndef _USB_H_
#define _USB_H_

typedef struct TDListRec {
	struct TDListRec* next;

	unsigned short stAddr;		// Where the HW copy of this struct lives

	unsigned short baseAddr;	// Where the data/payload for this TD lives
	unsigned short length;
	unsigned char port;
	unsigned char endpoint;
	unsigned char pid;
	unsigned char devAddr;
	unsigned char ctrl;
	unsigned char status;
	unsigned char retry;
	unsigned char residue;
} TDList;

typedef struct USBDevRec {
	// USB device configuration data
	short port;
	int dev;

	// USB device transport state	
	int dToggleIn;
	int dToggleOut;

	// Bulk transport configuration data
	int bulkface;
	int bulkin;
	int bulkout;

	// Bulk transport state
	int seq;
} USBDev;

extern void initbulkdev(USBDev* dev, short port);
extern void readblocks(USBDev* dev, int blocknum, int blockcount, char *outBuf);
extern void writeblocks(USBDev *dev, int blocknum, int blockcount, char *inBuf);

#endif /* _USB_H_ */
