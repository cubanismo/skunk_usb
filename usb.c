// Jaguar/Skunkboard USB mass storage driver
//
// Uses the USB functionality in the EZHost BIOS, accessed from the Jaguar
// using HPI mode on the skunkboard.

#include "usb.h"

/* From sprintf.c */
extern int printf(const char *fmt, ...);
extern int sprintf(char *str, const char *fmt, ...);

#define		haddr		((volatile short*)0xC00000)[0]
#define		hread		((volatile short*)0xC00000)[0]
#define		hreadd		((volatile int*)0xC00000)[0]
#define		hwrite		((volatile short*)0x800000)[0]
#define		hwrited		((volatile int*)0x800000)[0]
#define		hboxw(_x)	{haddr=0x4005; hwrite=(_x); haddr=0x4004;}
#define		hw(_a, _x)	{haddr=(_a); hwrite=(_x);}

static const int verbose = 0;	// Set to >= 1 for more verbose printing
#define		LOGV(fmt, ...) if (verbose > 0) printf(fmt, ##__VA_ARGS__)

#define		null		0
#define		uchar		unsigned char

#define		assert(_x)	{ if (!(_x)) { printf(#_x "\r\n"); while (1); } }

/* EZHost register addresses */
#define		SIE1msg		0x0144
#define		SIE2msg		0x0148
#define		LCP_INT		0x01c2
#define		LCP_R0		0x01c4	// LCP_Rxx registers used as params to interupts
#define		LCP_R1		0x01c6

/* EZHost TD semaphore addresses */
#define		SIE1_curTD	0x01b0
#define		SIE2_curTD	0x01b2
#define		HUSB_EOT	0x01b4
#define		SIE1_TDdone	0x01b6
#define		SIE2_TDdone	0x01b8

/* EZHost HPI Status Register Bits */
#define		HPI_mboxout	0x0001
#define		HPI_reset1	0x0002
#define		HPI_done1	0x0004
#define		HPI_done2	0x0008
#define		HPI_SIE1msg	0x0010
#define		HPI_SIE2msg	0x0020
#define		HPI_resume1	0x0040
#define		HPI_resume2	0x0080
#define		HPI_mboxin	0x0100
#define		HPI_reset2	0x0200
#define		HPI_sofeop1	0x0400
//			Reserved	0x0800
#define		HPI_sofeop2	0x1000
//			Reserved	0x2000
#define		HPI_id		0x4000
#define		HPI_vbus	0x8000

/* EZHost SIE messages */
#define		SIE_EP0int	0x0001
#define		SIE_EP1int	0x0002
#define		SIE_EP2int	0x0004
#define		SIE_EP3int	0x0008
#define		SIE_EP4int	0x0010
#define		SIE_EP5int	0x0020
#define		SIE_EP6int	0x0040
#define		SIE_EP7int	0x0080
#define		SIE_pReset	0x0100
#define		SIE_pSOF	0x0200
#define		SIE_pNoSOF	0x0800
#define		SIE_hDone	0x1000

/* EZHost Misc. Constants */
#define		TD_SIZE		0xc

/* Convenience defines for TDList members */
#define PID_SETUP		0xD
#define PID_IN			0x9
#define PID_OUT			0x1
#define PID_SOF			0x5
#define PID_PREAMBLE	0xC
#define PID_NAK			0xA
#define PID_STALL		0xE
#define PID_DATA0		0x3
#define PID_DATA1		0xB

#define CTRL_ARM		0x01
#define CTRL_ISO		0x10
#define CTRL_SYNSOF		0x20
#define CTRL_DTOGGLE	0x40
#define CTRL_PREAMBLE	0x80

#define STATUS_ACK		0x01
#define STATUS_ERROR	0x02
#define STATUS_TIMEOUT	0x04
#define STATUS_SEQ		0x08
#define STATUS_OVERFLOW	0x20
#define STATUS_NAK		0x40
#define STATUS_STALL	0x80
#define STATUS_ERROR_MASK \
	(STATUS_ERROR | \
	 STATUS_TIMEOUT | \
	 STATUS_OVERFLOW | \
	 STATUS_NAK | \
	 STATUS_STALL)

#define TT_CONTROL		0x0
#define TT_ISO			0x1
#define TT_BULK			0x2
#define TT_INTERRUPT	0x3

#define IS_ACTIVE(td) (((td)->retry & 0x10) >> 4)
#define GET_RETRY_COUNT(td) ((td)->retry & 0x3)

#define INIT_TD(td, start, base, len, dev, _port, ep, _pid, ttype, dt) \
	do { \
		(td)->stAddr = (start); \
		(td)->baseAddr = (base); \
		(td)->port = (_port); \
		(td)->length = (len); \
		(td)->pid = (_pid); \
		(td)->endpoint = (ep); \
		(td)->devAddr = (dev); \
		(td)->ctrl = CTRL_ARM | ((dt) ? CTRL_DTOGGLE : 0); \
		(td)->status = 0; \
		(td)->retry = (1<<4) /*active*/ | ((ttype) << 2) | 3; \
		(td)->residue = 0; \
		(td)->next = null; \
	} while (0)

/* Helper macros to encode TDList members to HW format */
#define MK_PORT_LENGTH(td) (((unsigned short)(td)->port<<14) | (td)->length)
#define MK_PID_EP(td) (((td)->pid<<4) | (td)->endpoint)
#define MK_NEXT_TD(td) ((td)->next ? (td)->next->stAddr : 0x0000)
#define MK_WORD(hi, lo) (((unsigned char)(hi) << 8) | (lo))
#define MK_DWORD(hi, lo) (((unsigned int)(lo) << 16) | (hi)) // LE, word-swapped

/*
 * Write a TDList structure to the EZHost, encoding it to HW form in the
 * process.
 */
static void writeTD(const TDList* td)
{
	haddr = td->stAddr;

	hwrited = MK_DWORD(MK_PORT_LENGTH(td), td->baseAddr);
	hwrited = MK_DWORD(MK_WORD(td->status, td->ctrl),
					   MK_WORD(td->devAddr, MK_PID_EP(td)));
	hwrited = MK_DWORD(MK_NEXT_TD(td), MK_WORD(td->residue, td->retry));
}

/* Write a list of TDList structures to the EZHost. */
static void writeTDList(const TDList* td)
{
	while (td) {
		writeTD(td);
		td = td->next;
	}
}

/*
 * Read a TDList structure back from the EZHost, decoding it from HW form in the
 * process.
 *
 * The stAddr field must be initialized when calling this function, and this
 * function does not touch the next, PID, endpoint, port, length, or base
 * address fields. The rest of the structure is clobbered.
 */
static void readTD(TDList* td)
{
	unsigned int tmp;
	unsigned short w;

	// Don't bother reading port, length, base addr, PID, or endpoint. They
	// don't change
	haddr = td->stAddr + 6;

	tmp = hreadd;
	w = tmp >> 16;
	td->ctrl = w & 0xff;
	td->status = w >> 8;
	w = tmp & 0xffff;
	td->retry = w & 0xff;
	td->residue = w >> 8;
}

/*
 * Reset to default and re-send to the EZHost just the fields in a TDList that
 * the EZHost BIOS changes.
 *
 * This is generally used to retry a transaction after a NAK or other
 * recoverable error.
 */
static void resetTD(TDList* td)
{
	// Only control, status, retry count, and residue are changed by the EZHost
	// BIOS. Assume the others are still valid.
	td->ctrl |= CTRL_ARM;
	td->status = 0;
	td->retry |= (1<<4) /*active*/ | 3;
	td->residue = 0;

	haddr = td->stAddr + 6;
	hwrited = MK_DWORD(MK_WORD(td->residue, td->retry),
					   MK_WORD(td->status, td->ctrl));
}

struct EZBlock {
	/* The first address in this block */
	short startaddr;
	/* number of bytes the block owns */
	short numbytes;
	char isfree;

	/* Active blocks are in a linked list */
	struct EZBlock *next;
	struct EZBlock *prev;
};

/* EZHost Internal Memory Map, from the EZHost BIOS manual and JCP source:
 *
 *  0000 - 04A3 - Interrupt vectors, HW Registers, and USB buffers
 *  04A4 - 0FEF - Free, reserved for GDB
 *  0FF0 - 102B - trubow (accelerated USB block copy stub)
 *  102C - 17FF - Free
 *  1800 - 37FF - JCP <-> EZHost communications buffers 1 & 2
 *  3800 - 3FFF - Free
 *
 * The EZHeap makes both of the free regions listed above available for use as
 * USB TDList/data staging memory.
 */
#define EZHEAP_REG0_MIN		0x102C
#define EZHEAP_REG0_SIZE	0x7D4
#define EZHEAP_REG1_MIN		0x3800
#define EZHEAP_REG1_SIZE	0x800
#define EZHEAP_BLOCKS 16 // Must be >= 2
static struct EZHeap {
	struct EZBlock blocks[EZHEAP_BLOCKS];
	struct EZBlock *region0;
	struct EZBlock *region1;
} ezheap;

static void init_ezheap(void)
{
	short i;

	ezheap.blocks[0].startaddr = EZHEAP_REG0_MIN;
	ezheap.blocks[0].numbytes = EZHEAP_REG0_SIZE;
	ezheap.blocks[0].isfree = 1;
	ezheap.blocks[0].next = &ezheap.blocks[0];
	ezheap.blocks[0].prev = &ezheap.blocks[0];
	ezheap.region0 = &ezheap.blocks[0];
	ezheap.blocks[1].startaddr = EZHEAP_REG1_MIN;
	ezheap.blocks[1].numbytes = EZHEAP_REG1_SIZE;
	ezheap.blocks[1].isfree = 1;
	ezheap.blocks[1].next = &ezheap.blocks[1];
	ezheap.blocks[1].prev = &ezheap.blocks[1];
	ezheap.region1 = &ezheap.blocks[1];
	for (i = 2; i < EZHEAP_BLOCKS; i++) {
		ezheap.blocks[i].next = ezheap.blocks[i].prev = null;
	}
}

#define EZHEAP_ITER_BEGIN(_block) (_block) = ezheap.region0; do {
#define EZHEAP_ITER_END(_block) \
	(_block) = (_block)->next; \
	if ((_block) == ezheap.region0) (_block) = ezheap.region1; \
	else if ((_block) == ezheap.region1) break; \
} while (1)

#define EZBLOCK_IS_HEAD(_block) \
	(((_block) == ezheap.region0) || ((_block) == ezheap.region1))

static void dump_ezheap(void)
{
	struct EZBlock *block;
	int i = 0;
	LOGV("-- Dumping EZHeap --\r\n");
	LOGV("region0 = 0x%08x next = 0x%08x prev = 0x%08x\r\n",
		 ezheap.region0, ezheap.region0->next, ezheap.region0->prev);
	LOGV("region1 = 0x%08x next = 0x%08x prev = 0x%08x\r\n",
		 ezheap.region1, ezheap.region1->next, ezheap.region1->prev);

	EZHEAP_ITER_BEGIN(block) {
		LOGV("  block %d isfree = %s start = 0x%04x bytes = %d\r\n",
			 i++, block->isfree ? "yes" : "no", block->startaddr,
			 block->numbytes);
		LOGV("  block ptr = 0x%08x next = 0x%08x prev = 0x%08x\r\n",
			 block, block->next, block->prev);
	} EZHEAP_ITER_END(block);

	LOGV("-- Done Dumping EZHeap --\r\n");
	haddr = 0x4004;
}

static struct EZBlock *ezheap_split_block(struct EZBlock *block, short bytes)
{
	struct EZBlock *tmp = null;
	int i;

	assert(bytes < block->numbytes);
	/* Only free blocks can be split */
	assert(block->isfree);

	for (i = 0; i < EZHEAP_BLOCKS; i++) {
		tmp = &ezheap.blocks[i];
		if (tmp->next == null) {
			assert(tmp->prev == null);

			tmp->prev = block;
			tmp->next = block->next;
			tmp->next->prev = tmp;
			block->next = tmp;

			tmp->startaddr = block->startaddr + bytes;
			tmp->numbytes = block->numbytes - bytes;
			tmp->isfree = block->isfree;

			block->numbytes = bytes;

			return block;
		}
	}

	/* No unused blocks available */
	return null;
}

static short get_ezheap(short bytes)
{
	struct EZBlock *block;

	LOGV("Getting ezheap bytes %d\r\n", bytes);

	/* Round bytes up to the nearest word */
	bytes = (bytes + 1) & ~1;

	EZHEAP_ITER_BEGIN(block) {
		if (block->isfree) {
			if (block->numbytes == bytes) {
				block->isfree = 0;
				dump_ezheap();
				return block->startaddr;
			} else if (block->numbytes > bytes) {
				block = ezheap_split_block(block, bytes);

				if (block) {
					block->isfree = 0;
					dump_ezheap();
					return block->startaddr;
				}
				/* Else, we're out of unused blocks. Keep looking in case
				 * there's a free block exactly the size of the request. */
			}
		}
	} EZHEAP_ITER_END(block);

	dump_ezheap();
	return -1;
}

static void ezheap_merge_block(struct EZBlock *block)
{
	struct EZBlock *tmp = block->next;

	/* Only free blocks can be merged */
	assert(block->isfree);

	if (tmp->isfree && !EZBLOCK_IS_HEAD(tmp)) {
		assert(tmp != block);
		block->numbytes += tmp->numbytes;
		tmp->prev = null;
		tmp->next->prev = block;
		block->next = tmp->next;
		tmp->next = null;
	}

	tmp = block->prev;
	if (tmp->isfree && !EZBLOCK_IS_HEAD(block)) {
		assert(tmp != block);
		tmp->numbytes += block->numbytes;
		block->prev = null;
		tmp->next = block->next;
		tmp->next->prev = tmp;
		block->next = null;
	}
}

static void return_ezheap(short addr, short bytes)
{
	struct EZBlock *block;

	LOGV("Returning ezheap addr 0x%04x size %d\r\n", addr, bytes);

	EZHEAP_ITER_BEGIN(block) {
		if (block->startaddr == addr) {
			assert(!block->isfree);
			assert(((bytes + 1) & ~1) == block->numbytes);

			block->isfree = 1;
			ezheap_merge_block(block);
			dump_ezheap();
			return;
		}

		block = block->next;
	} EZHEAP_ITER_END(block);

	dump_ezheap();
	assert(!"Unable to return block to EZHeap!");
}

static int initusbdev(USBDev *dev);
static int run(const short* p, short mbox);
static int usbctlmsg(USBDev *dev, uchar rtype, uchar request, short value, short index, char* buffer, short len);
static int bulkcmd(USBDev *dev, uchar opcode, int blocknum, int blockcount, char* buffer, int len);

static int zero = 0;

void initbulkdev(USBDev *dev, short port)
{
	char buf[128];
	char str[17];
	int s1, s2, s3, s4, i;

	zero = 0;		// Until BSS starts working

	dev->port = port;
	dev->dev = 0;
	dev->dToggleIn = 0;
	dev->dToggleOut = 0;
	dev->seq = 0x00000001;
	
	// General initialization and enumeration
	if (initusbdev(dev)) {
		assert(!"USB Device Initialization failed");
	}

	printf("\r\nUSB Device Initialized on port %d\r\n\r\n", port);

	s1 = usbctlmsg(dev, 0x21, 0xff, 0, dev->bulkface, null, 0);	// Reset the device
	assert(0 == s1);	// NAKs?

	// Test unit ready
	s2 = bulkcmd(dev, 0, 0, 0, null, 0);
	LOGV("bulkcmd returned 0x%08x\r\n", s2);
	assert(0 == s2);

	// Make an INQUIRY
	s3 = bulkcmd(dev, 0x12, 0, 0, buf, 36);
	LOGV("bulkcmd returned 0x%08x\r\n", s3);
	assert(0 == s3);
	printf("Peripheral Device Type: 0x%02x\r\n", buf[0] & 0x1f);
	printf("Removable Media? %s\r\n", (buf[1] & 0x80) ? "yes" : "no");
	for (i = 0; i < 8; i++) {
		str[i] = buf[8 + i];
	}
	str[i] = '\0';
	printf("Vendor ID: %s\r\n", str);
	for (i = 0; i < 16; i++) {
		str[i] = buf[16 + i];
	}
	str[i] = '\0';
	printf("Product ID: %s\r\n", str);
	for (i = 0; i < 4; i++) {
		str[i] = buf[32 + i];
	}
	str[i] = '\0';
	printf("Product Revision Level: %s\r\n", str);

	// Get disk size and block size
	s4 = bulkcmd(dev, 0x25, 0, 0, buf, 8);
	LOGV("bulkcmd returned 0x%08x\r\n", s4);
	assert(0 == s4);
	printf("Last Logical Block Address: 0x%08x\r\n", *(unsigned int *)&buf[0]);
	printf("Block Length in Bytes: 0x%08x\r\n", *(unsigned int *)&buf[4]);
}

void readblocks(USBDev *dev, int blocknum, int blockcount, char *outBuf)
{
	int res;

	res = bulkcmd(dev, 0x28, blocknum, blockcount, outBuf,
				  blockcount * 0x200 /* XXX hard-coded block size */);
	LOGV("readblocks(): bulkcmd returned 0x%08x\r\n", res);
	assert(0 == res);
	haddr = 0x4001;
}

void writeblocks(USBDev *dev, int blocknum, int blockcount, char *inBuf)
{
	int res;

	res = bulkcmd(dev, 0x2a, blocknum, blockcount, inBuf,
				  blockcount * 0x200 /* XXX hard-coded block size */);
	LOGV("writeblocks(): bulkcmd returned 0x%08x\r\n", res);
	assert(0 == res);
	haddr = 0x4001;
}

// Convert a USB-style 16-bit-word Unicode string to ascii
static void utf16letoascii(char *out, const char *in, int length)
{
	const unsigned short *utf = (const unsigned short *)in;
	int i;
	for (i = 0; i < (length >> 1); i++) {
		// USB uses little endian, so byte-swap the word
		unsigned short swapped = (utf[i] >> 8) | (utf[i] << 8);
		if (swapped < 0x20 || swapped > 0x7e) {
			out[i] = '*';
		} else {
			out[i] = swapped;
		}
	}
	out[i] = '\0';
}

int inithusb(void)
{
	int s1;

	// Initialize the CY16 for Host USB on SIE1
	static const short husb_init[] = {
		SIE1msg,	0,		// Clear SIE1msg
		//0xc090,	-1,		// Clear USB interrupts - Needs control register access
		HUSB_EOT,	4800,	// EOT is 4800 bit times (unlikely)
		0x142, 		0x440,	// HPI intr routing reg: Don't put anything on HPI pin
		//0xc0c8,	0x30,	// Hear about connect change events - Also a control reg
		LCP_INT,	0x72,	// HUSB_SIE1_INIT_INT - No regs, no return val
		0, 0				// End sequence.
	};

	// Turn on host USB ports (SIE1)
	s1 = run(husb_init, 0xce01);	// Init HUSB on SIE1
	if (s1 != 0xfed) {
		return -1;
	}

	init_ezheap();

	return 0;
}

static int initusbdev(USBDev *dev)
{
	// Force a RESET on SIE1
	const short port_reset[] = {
		LCP_R1,	dev->port,	// r1 - Port number (0 = left, 1 = right port on skunk)
		LCP_R0,	0x3c,		// r0 - Reset duration in milliseconds
		LCP_INT, 0x74,		// Run HUSB_RESET_INT
		0, 0				// End sequence. Note: Caller to read return val in r0
	};
	int s1, s2, s3, s4, s5, s6;
	int i, j, k;
	int retry;
	char test[256], *p = test;
	char ascii[128];
	int manufStrIdx, prodStrIdx, serialStrIdx;
	int numconfigs, numfaces, numends, setconfig;
	int isbulk=0;
	char enSupported = 0;
	uchar devNum;

	s1=s2=s3=s4=s5=s6=0;
	dev->bulkin=dev->bulkout=-1;
	s1 = run(port_reset, 0xce01); 	// Reset SIE1 port <dev->port>
	if (s1 != 0xfed) {
		return -1;
	}

	// See if a device was found
	haddr = LCP_R0;
	i = hread;

	if (i & 0x2) {
		printf("No device connected\r\n");
		return -2;
	} else {
		// Pause to give the device some time to get ready after reset.
		// Some devices seem to need this, some don't.
		for (i = 0; i < 1000000; i++);
	}

	printf("%s-speed device detected on port %d\r\n",
		   (i & 1) ? "Low" : "Full", dev->port);

	devNum = 2;
	s2 = usbctlmsg(dev, 0, 5, devNum, 0, null, 0);	// Set USB device address to 2
	assert(0==s2);
	dev->dev = devNum;
	retry = 0;
	do {
		s3 = usbctlmsg(dev, 0x80, 6, 0x100, 0, (char*)test, 18); 	// Read device descriptor
	} while (((0 != s3) || (1 != test[1])) && (retry++ < 100));
	assert(0==s3 && 1==test[1]);					// Make sure we got one
	numconfigs = test[17];
	
	if (0 == numconfigs) {		// Yay jumpdrive!  You are so broken...
		printf("Zero configs?!?\r\n");
		isbulk = 1;
		dev->bulkin = 1;
		dev->bulkout = 1;
	}

	// Get string language support
	s4 = usbctlmsg(dev, 0x80, 6, 0x300, 0, (char*)test, 1); 	// Read device string language list size
	if (!s4) {
		int size = test[0];
		const unsigned short *words = (const unsigned short *)&test[2];
		s4 = usbctlmsg(dev, 0x80, 6, 0x300, 0, (char*)test, size); 	// Read device string language list

		printf("Language IDs supported:\r\n");
		for (i = 0; i < (test[0] - 2); i++ ) {
			// Returned in little-endian, so swap:
			unsigned short langID = (words[i] << 8) | (words[i] >> 8);
			printf("    0x%04x\r\n", langID);
			if (langID == 0x409) // English (United States)
				enSupported = 1;
		}
	}

	if (enSupported) {
		// Print out some device info strings
		manufStrIdx = test[14];
		prodStrIdx = test[15];
		serialStrIdx = test[16];
		s4 = usbctlmsg(dev, 0x80, 6, 0x300 + manufStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s4) {
			int size = test[0];
			s4 = usbctlmsg(dev, 0x80, 6, 0x300 + manufStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s4 == 0);
			utf16letoascii(ascii, &test[2], size - 2);
			printf("Device Manufacturer:  %s\r\n", ascii);
		} else {
			printf("Failed to query device manufacturer");
		}

		s4 = usbctlmsg(dev, 0x80, 6, 0x300 + prodStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s4) {
			int size = test[0];
			s4 = usbctlmsg(dev, 0x80, 6, 0x300 + prodStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s4 == 0);
			utf16letoascii(ascii, &test[2], size - 2);
			printf("Device Product Name:  %s\r\n", ascii);
		} else {
			printf("Failed to query device product name");
		}

		s4 = usbctlmsg(dev, 0x80, 6, 0x300 + serialStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s4) {
			int size = test[0];
			s4 = usbctlmsg(dev, 0x80, 6, 0x300 + serialStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s4 == 0);
			utf16letoascii(ascii, &test[2], size - 2);
			printf("Device Serial Number: %s\r\n", ascii);
		} else {
			printf("Failed to query device serial number");
		}
	}
	
	// Parse all descriptors until we find the bulk interface
	for (i = 0; i < numconfigs && !isbulk; i++) {
		s5 = usbctlmsg(dev, 0x80, 6, 0x200 + i, 0, (char*)test, 32);	// Read config descriptor
		assert(0==s5 && 2==test[1]);							// Make sure we got one
		
		p = test;			// Parse config packet
		numfaces = p[4];	// Number of interfaces for this config
		setconfig = p[5];	// Configuration number 
		p += p[0];			// Next descriptor
		
		for (j = 0; j < numfaces && !isbulk; j++) {	// Parse all interfaces
			assert(4==p[1]);	// Make sure we got one
			dev->bulkface = p[2];	// Interface number
			numends = p[4];		// Number of endpoints in this interface
			isbulk = (0x8 == p[5] && 0x50 == p[7]);	// USB Mass Storage Class + Bulk Only Interface
			if (isbulk) {
				printf("Found bulk interface, interface subclass: 0x%02x\r\n", p[6]);
			}
			p += p[0];		// Next descriptor
			
			for (k = 0; k < numends; k++) {	// Parse all endpoints
				assert(5==p[1]);	// Make sure we got one
				if (isbulk && 2==p[3]) {
					if (0x80 & p[2])
						dev->bulkin = p[2]&0x7f;
					else
						dev->bulkout = p[2]&0x7f;
					assert(p[4]==64);	// 64 byte packets, right?  Even for 2.0 devices?  ???
				}
				p += p[0];	// Next descriptor
			}
		}
	}
	
	// Make sure we really got them
	assert(isbulk && dev->bulkin>0 && dev->bulkout>0);

	LOGV("isbulk: %d bulkin: %d bulkout %d\r\n",
		 isbulk, dev->bulkin, dev->bulkout);
	
	// Set our favorite configuration -- we are enumerated baby!
	s6 = usbctlmsg(dev, 0, 9, setconfig, 0, null, 0);
	assert(0 == s6);

	return 0;
}

static int waitSIE1done(void)
{
	int i = 100000;
	int status, msg;

	do {
		haddr = 0x4007;
		status = 0xdead0000 | hwrite;

		LOGV("polling for SIE1 msg, i = %d, HPI Status: 0x%08x\r\n", i, status);

		if (status & HPI_mboxout) {
			/* Clear HPI mailbox flag by reading mailbox */
			haddr = 0x4005;
			msg = hwrite;
		}

		haddr = 0x4004;
		if (status & HPI_SIE2msg) {
			/* Clear SIE2 message flag by reading message */
			haddr = SIE2msg;
			msg = hread;
		}

		if (status & HPI_SIE1msg) {
			/* Clear SIE1 message flag by reading message */
			haddr = SIE1msg;
			msg = hread;
			break;
		}

		msg = 0;
	} while (i--);

	if (msg & SIE_hDone) {
		/* Read and clear SIE1 TD done semaphore */
		haddr = SIE1_TDdone;
		msg = hread;
		haddr = SIE1_TDdone;
		hwrite = 0;

		if (msg != 1) {
			printf("*** Got Done Message but HUSB_SIE_pTDListDoneSem not set!\r\n");
			haddr = 0x4004;
		}

		return 0;
	} else if (msg) {
		return msg;
	}

	return status;
}

#define WAIT_BULK_SUCCESS			0x00000000
#define WAIT_BULK_RETRY				0xf33df33d

static int waitbulktdlist(USBDev *dev, TDList *td, int direction)
{
	int status = waitSIE1done();
	if (status)
		return status;

	readTD(td);
	LOGV("--Control=0x%02x Status=0x%02x RetryCn=0x%02x Residue=0x%02x\r\n",
		 (unsigned)td->ctrl, (unsigned)td->status,
		 (unsigned)td->retry, (unsigned)td->residue);
	haddr = 0x4004;

	if ((td->status & STATUS_ERROR_MASK) == STATUS_NAK) {
		// Device NAKed. Retry.
		return WAIT_BULK_RETRY;
	} else if ((td->status & STATUS_ERROR_MASK) || IS_ACTIVE(td)) {
		printf("Wait for Bulk TDList failed: "
			   "Control=0x%02x Status=0x%02x Retry=0x%02x Residue=0x%02x\r\n",
			   (unsigned)td->ctrl, (unsigned)td->status,
			   (unsigned)td->retry, (unsigned)td->residue);
		haddr = 0x4004;
		return (td->status << 4) | IS_ACTIVE(td);
	}

	if (direction) {
		dev->dToggleIn ^= 1;
	} else {
		dev->dToggleOut ^= 1;
	}

	return WAIT_BULK_SUCCESS;
}

/**
 * Executes a bulk mass storage command on bulkin/bulkout
 * Expects to read len bytes of data unless opcode = SCSI write
 * Do not exceed 64KB in len or 16M in blocknum or 256 in blockcount -- these are not decoded properly
 */
int bulkcmd(USBDev *dev, uchar opcode, int blocknum, int blockcount, char* buffer, int len)
{
	TDList td;
	int i = 0, sie, retry;
	// 0 = write, 0x80 = read:
	int direction = (opcode == 0xff || opcode == 0x2a) ? 0x0 : 0x80;
	unsigned short t;
	short ezaddr, ezdata;
#define DT(a) (dev->dToggle##a)

	haddr = 0x4004;

	// Command needs 31 bytes but we'll write 32 since we use word/dword writes.
	ezaddr = get_ezheap(0xc + 32);
	assert(ezaddr >= 0);

	haddr = ezaddr;			// TD list base

	// Build CBW - TD data is always 31 bytes at ezaddr + 0x0c
	INIT_TD(&td, ezaddr, ezaddr + 0xc, 31, dev->dev, dev->port, dev->bulkout, PID_OUT, TT_BULK, DT(Out));
	writeTD(&td);
	hwrited = 0x53554342;				// ezaddr+0x0c: CBW signature (43425355 in little endian land)
	hwrited = (dev->seq >> 16)|(dev->seq << 16);	// ezaddr+0x10: Command Block Tag (it's a sequence number)
	dev->seq++;
	hwrited = len<<16;					// ezaddr+0x14: Transfer length (little endian)
	hwrite = direction;					// ezaddr+0x18: Direction + LUN (zero)
	hwrite = 0xc | (opcode<<8);			// ezaddr+0x1a: We support the 12-byte USB Bootability SCSI subset
	switch (opcode) {
	case 0x2A: // Write
		/* fall through */
	case 0x28: // Read
		hwrited = (blocknum&0xff00) | (blocknum>>16);		// ezaddr+0x1c: Middle-endian:  MSB=0, LUN=0, 1SB=>>8, 2SB=>>16
		hwrited = ((blocknum&0xff)<<16) | (blockcount<<8);	// ezaddr+0x20: Middle-endian:  Rsv=0, LSB=>>24, LSB, MSB
		hwrited = zero;					// ezaddr+0x24: Reserved fields
		hwrited = zero;					// ezaddr+0x28: Remainder of the CBW (unused with 12 byte commands)
		break;
	case 0x12: // Inquiry
		hwrite = 0;		// ezaddr+0x1c: 2,1: LUN = 0, reserved byte = 0.
		hwrite = 0x24 << 8;	// ezaddr+0x1e: 4,3: reserved byte = 0, allocation length = 0x24
		hwrite = zero;	// ezaddr+0x20: 6,5: reserved byte = 0, pad byte = 0
		hwrite = zero;	// ezaddr+0x22: 8,7: pad bytes = 0
		hwrite = zero;	// ezaddr+0x24: 10,9: pad bytes = 0
		hwrite = zero;	// ezaddr+0x28: 12,11: pad bytes = 0
		break;
	default:
		printf("Warning: Unhandled bulkcmd opcode: 0x%02x\r\n", opcode);
		haddr = 0x4004;
		haddr = ezaddr + 0x1c;
		/* fall through */
	case 0x00: // Test unit ready
		/* fall through */
	case 0x25:	// Read capacity
		hwrited = zero;	// ezaddr+0x1c: LUN = 0, reserved
		hwrited = zero;	// reserved/pad
		hwrited = zero;	// reserved/pad
		hwrited = zero;	// unused.
		break;
	}

	retry = 1;
	for (i = 0; i < 1000 && retry; i++) {
		retry = 0;
		hw(SIE1_curTD, ezaddr);	// Execute our new TD

		LOGV("--Executing CBW\r\n");
		haddr = 0x4004;

		sie = waitbulktdlist(dev, &td, 0);

		if (sie == WAIT_BULK_RETRY) {
			resetTD(&td);
			retry = 1;
		}
	}

	/* Return EZHost memory from CBW transfer */
	return_ezheap(ezaddr, 0xc + 32);

	if (sie != WAIT_BULK_SUCCESS) {
		return sie;
	}

	if (len) {
		/* Get EZHost memory for data */
		ezdata = get_ezheap(len);
		assert(ezdata >= 0);
	}

	if (!(direction & 0x80) && len) {	// We have data to write
		int olen = len;
		haddr = ezdata;
		while (olen >= 0) {
			t = (unsigned short)(uchar)buffer[0] | (unsigned short)buffer[1] << 8; // Endian swap
			hwrite = t;
			buffer += 2;
			olen -= 2;
		}
	}

	// Build data section (if there is one)
	if (len) {
		unsigned short curMemAddr = ezdata;
		int remainder = len;
		unsigned short curChunk = 0;
		while (remainder > 0) {
			curChunk = (remainder >= 64) ? 64 : remainder;
			ezaddr = get_ezheap(0xc);
			assert(ezaddr >= 0);
			retry = 1;

			INIT_TD(&td, ezaddr, curMemAddr, curChunk, dev->dev, dev->port,
					direction ? dev->bulkin : dev->bulkout,
					direction ? PID_IN : PID_OUT, TT_BULK,
					direction ? DT(In) : DT(Out));
			writeTD(&td);

			for (i = 0; i < 1000 && retry; i++) {
				retry = 0;
				hw(SIE1_curTD, ezaddr);				// Execute our new TD

				LOGV("--Executing Data %s\r\n", direction ? "IN" : "OUT");
				haddr = 0x4004;

				sie = waitbulktdlist(dev, &td, direction);

				if (sie == WAIT_BULK_RETRY) {
					resetTD(&td);
					retry = 1;
				}
			}

			return_ezheap(ezaddr, 0xc);

			if (sie != WAIT_BULK_SUCCESS) {
				return_ezheap(ezdata, len);
				return sie;
			}
			remainder -= curChunk;
			curMemAddr += curChunk;
		}
	}

	// TD header is 12 bytes, CSW data is 13 bytes
	ezaddr = get_ezheap(0xc + 13);
	assert(ezaddr >= 0);

	// Build CSW - TD data is always 13 bytes
	INIT_TD(&td, ezaddr, ezaddr+0xc, 13, dev->dev, dev->port,
			dev->bulkin, PID_IN, TT_BULK, DT(In));
	writeTD(&td);

	retry = 1;
	for (i = 0; i < 1000 && retry; i++) {
		retry = 0;

		hw(SIE1_curTD, ezaddr);				// Execute our new TD

		LOGV("--Executing CSW\r\n");
		haddr = 0x4004;

		sie = waitbulktdlist(dev, &td, 0x80);

		if (sie == WAIT_BULK_RETRY) {
			resetTD(&td);
			retry = 1;
		}
	}

	if (sie != WAIT_BULK_SUCCESS) {
		if (len) {
			return_ezheap(ezdata, len);
		}
		return_ezheap(ezaddr, 0xc + 13);
		return sie;
	}

	if (len) {
		if (direction & 0x80) {	// We have data to read
			int ilen = len;
			haddr = ezdata;
			while (ilen >= 0) {
				t = hread;
				buffer[0] = t;				// Endian swap
				buffer[1] = t >> 8;
				buffer += 2;
				ilen -= 2;
			}
		}

		return_ezheap(ezdata, len);
	}

	haddr = ezaddr + 0xc;
	i = hreadd;
	if (i != 0x53555342) {
		printf("Invalid CSW signature: 0x%08x\r\n", i);
		haddr = 0x4004;
		haddr = 0x1548;
	}
	i = hreadd;
	i = (i << 16) | (i >> 16);
	if (i != dev->seq - 1) {
		printf("Invalid CSW tag: 0x%08x\r\n", i);
		printf("      Should be: 0x%08x\r\n", dev->seq - 1);
		haddr = 0x4004;
		haddr = 0x154c;
	}
	i = hreadd;
	i = (i >> 16) | (i << 16);
	if (i != 0) {
		printf("CSW Residue: 0x%08x\r\n", i);
		haddr = 0x4004;
		haddr = 0x1550;
	}
	i = hread;
	i &= 0xff;
	if (i != 0) {
		printf("CSW status indicates failure: %d\r\n", i);
	}

	return_ezheap(ezaddr, 0xc + 13);
	
	return sie;		
}

/**
 * Executes a generic USB Control Message, including optional data
 * The data direction (input or output) is determined by rtype & 0x80 (0x80 means read from device)
 * 
 * Returns:
 *   0 mean success -- all other return values indicate various trouble
 *   Errors like 0xdead.... mean the TD never executed
 *   Errors like 0x..000010 mean the TD failed due to a USB protocol error
 *		0 Ack Transmission acknowledge
 *		1 Error Error detected in transmission
 *		2 Time-Out Time Out occur
 *		3 Seq Sequence Bit. 0-DATA0, 1-DATA1
 *		4 Reserved
 *		5 Overflow Overflow condition – maximum length exceeded during receive (or Underflow condition)
 *		6 NAK Peripheral returns NAK
 *		7 STALL Peripheral returns STALL
 */
static int usbctlmsg(USBDev *dev, uchar rtype, uchar request, short value, short index, char* buffer, short len)
{
	TDList tdSetup, tdData, tdStatus;
	short ezaddr;
	short t;
	int status, olen = len;
	
	// Construct a TD list for a control message
	// See USB 2.0 spec section 8.5.3, figure 8-37 for details (p. 226)

	ezaddr = 0x1500;        // First TD list entry start address in EZHost RAM
	haddr = 0x4004;

	// Setup data is always 8 bytes located just after the TD list entry
	INIT_TD(&tdSetup, ezaddr, ezaddr + TD_SIZE, 8, dev->dev, dev->port, 0,
			PID_SETUP,
			TT_CONTROL,
			0);							// Setup packet is always DATA0
	tdSetup.next = &tdStatus;

	haddr = ezaddr + TD_SIZE;
	hwrite = rtype | (request<<8);	// Setup data: request (MSB), rtype (LSB)
	hwrite = value;
	hwrite = index;
	hwrite = len;

	ezaddr = tdSetup.baseAddr + tdSetup.length;

	if (len) {							// If we have data, add a data phase
		tdSetup.next = &tdData;
		INIT_TD(&tdData, ezaddr, 0 /* set later */, len, dev->dev, dev->port, 0,
				(rtype & 0x80) ? PID_IN : PID_OUT,
				TT_CONTROL,
				1);						// First data packet is always DATA1
		tdData.next = &tdStatus;
		ezaddr += TD_SIZE;
	}

	INIT_TD(&tdStatus, ezaddr, 0x0000, 0, dev->dev, dev->port, 0,
			// Opposite direction of data packet. No-data messages should always
			// end up with PID_IN here.
			(rtype & 0x80) ? PID_OUT : PID_IN,
			TT_CONTROL,
			1);							// Status packet is always DATA1
	ezaddr += TD_SIZE;

	tdData.baseAddr = ezaddr;			// Set tdData data address
	writeTDList(&tdSetup);				// Write out the entire TD list.

	if (!(rtype & 0x80))				// We have data to write
		while (len >= 0) {			
			hwrite = buffer[0] | (buffer[1]<<8);  // Endian swap
			buffer += 2;
			len -= 2;
		}

	hw(SIE1_curTD, tdSetup.stAddr);		// Execute our new TD

	// Wait for the TD list to be done
	status = waitSIE1done();
	if (status)
		return status;

	readTD(&tdSetup);
	LOGV("--setup: "
		 "Control=0x%02x Status=0x%02x RetryCnt=0x%02x Residue=0x%02x\r\n",
		 (unsigned)tdSetup.ctrl, (unsigned)tdSetup.status,
		 (unsigned)tdSetup.retry, (unsigned)tdSetup.residue);
	haddr = 0x4004;
	if ((tdSetup.status & STATUS_ERROR_MASK) || IS_ACTIVE(&tdSetup)) {
		printf("Control message setup phase failed: "
			   "Control=0x%02x Status=0x%02x Retry=0x%02x Residue=0x%02x\r\n",
			   (unsigned)tdSetup.ctrl, (unsigned)tdSetup.status,
			   (unsigned)tdSetup.retry, (unsigned)tdSetup.residue);
		haddr = 0x4004;
		return (tdSetup.status << 4) | 1;
	}

	readTD(&tdStatus);
	LOGV("--status: "
		 "Control=0x%02x Status=0x%02x RetryCnt=0x%02x Residue=0x%02x\r\n",
		 (unsigned)tdStatus.ctrl, (unsigned)tdStatus.status,
		 (unsigned)tdStatus.retry, (unsigned)tdStatus.residue);
	haddr = 0x4004;
	if ((tdStatus.status & STATUS_ERROR_MASK) || IS_ACTIVE(&tdStatus)) {
		printf("Control message status phase failed: "
			   "Control=0x%02x Status=0x%02x Retry=0x%02x Residue=0x%02x\r\n",
			   (unsigned)tdStatus.ctrl, (unsigned)tdStatus.status,
			   (unsigned)tdStatus.retry, (unsigned)tdStatus.residue);
		haddr = 0x4004;
		return (tdStatus.status << 4) | 2;
	}

	if (!olen)
		return 0;

	readTD(&tdData);
	LOGV("--data: "
		 "Control=0x%02x Status=0x%02x RetryCnt=0x%02x Residue=0x%02x\r\n",
		 (unsigned)tdData.ctrl, (unsigned)tdData.status,
		 (unsigned)tdData.retry, (unsigned)tdData.residue);
	haddr = 0x4004;
	if ((tdData.status & STATUS_ERROR_MASK) || IS_ACTIVE(&tdData)) {
		printf("Control message data phase failed: "
			   "Control=0x%02x Status=0x%02x Retry=0x%02x Residue=0x%02x\r\n",
			   (unsigned)tdData.ctrl, (unsigned)tdData.status,
			   (unsigned)tdData.retry, (unsigned)tdData.residue);
		haddr = 0x4004;
		return (tdData.status << 4) | 3;
	}

	if (rtype & 0x80) {				// We have data to read
		haddr = tdData.baseAddr;
		while (len >= 0) {
			t = hread;
			buffer[0] = t;			// Endian swap
			buffer[1] = t >> 8;
			buffer += 2;
			len -= 2;
		}
	}

	return 0;
}

static int run(const short* p, short mbox) {
	int i, mb, sie;
	
	haddr = 0x4004;
	while (*p != 0) {
		haddr = *p++;
		hwrite = *p++;
	}
	
	if (0 != mbox)
		hboxw(mbox);	  	
	
	// Wait for the result in HPI status port
	haddr = 0x4007;
	for (i = 100000; i && 0 == (hwrite & 17); i--);

	mb = sie = 0xdead0000 | hwrite;
	LOGV("i = %d, mb = 0x%08x sie = 0x%08x\r\n", i, mb, sie);
	haddr = 0x4007;
	
	i = hwrite;
	LOGV("Now i = 0x%08x\r\n", i);
	haddr = 0x4007;
	if (i & 1) {		// HPI mailbox
		haddr=0x4005; // Clear the mailbox out interrupt
		mb=hwrite;
		LOGV("Got MB: Now = 0x%08x\r\n", mb);
		haddr=0x4005;
	}
	
	haddr=0x4004;
	if (i & 16) {
		haddr=SIE1msg;
		sie=hread;
		LOGV("Got SIE1msg in run(): sie1 = 0x%08x\r\n", sie);
		haddr=0x4004;
	}
	if (i & 32) {
		haddr=SIE2msg;
		i=hread;
		LOGV("Got SIE2msg in run(): sie2 = 0x%08x\r\n", i);
		haddr=0x4004;
	}

	return (0 == mbox) ? sie : mb;		
}
