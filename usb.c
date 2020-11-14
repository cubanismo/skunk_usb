// Test file system access using HPI mode on Jaguar

#define		haddr		((volatile short*)0xC00000)[0]
#define		hread		((volatile short*)0xC00000)[0]
#define		hreadd		((volatile int*)0xC00000)[0]
#define		hwrite		((volatile short*)0x800000)[0]
#define		hwrited		((volatile int*)0x800000)[0]
#define		hboxw(_x)	{haddr=0x4005; hwrite=(_x); haddr=0x4004;}
#define		hw(_a, _x)	{haddr=(_a); hwrite=(_x);}
#define		checkbox()	{haddr=0x4005; result=hread; haddr=0x4004;}

#define 	SIE1msg		0x0144
#define 	SIE2msg		0x0148
#define		LCP_INT		0x01c2
#define		LCP_R0		0x01c4	// LCP_Rxx registers used as params to interupts
#define		LCP_R1		0x01c6

static const int verbose = 0;	// Set to >= 1 for more verbose printing
#define		LOGV(fmt, ...) if (verbose > 0) printf(fmt, ##__VA_ARGS__)

#define		null		0
#define 	uchar		unsigned char

#define		assert(_x)	{ if (!(_x)) { printf(#_x "\r\n"); while (1); } } 

extern int printf(const char *fmt, ...);
extern int sprintf(char *str, const char *fmt, ...);

#include "skunk.h"

static int inithusb1(int port);
static int run(short* p, short mbox);
static int usbctlmsg(uchar dev, uchar rtype, uchar request, short value, short index, char* buffer, short len);
static int bulkcmd(uchar opcode, int blocknum, int blockcount, char* buffer, int len);

static int bulkin, bulkout, bulkface, bulkdev;
static int seq = 0x00000001;
static int dToggleIn;
static int dToggleOut;
static int zero = 0;

void start() {
	char buf[2048];
	char str[100];
	char *sPtr;
	int s1, s2, s3, s4, s5, i, j;
	unsigned int d;
	char c;

	skunkRESET();
	skunkNOP();
	skunkNOP();

	printf("Staring up\r\n\r\n");
	
	zero = 0;		// Until BSS starts working
	dToggleIn = 0;
	dToggleOut = 0;
	seq = 0x00000001;
	
	// General initialization and enumeration
	if (inithusb1(0)) {
		assert(!"USB1 Initialization failed");
	}

	printf("\r\nUSB1 Initialized\r\n\r\n");

	s1 = usbctlmsg(bulkdev, 0x21, 0xff, 0, bulkface, null, 0);	// Reset the device
	assert(0 == s1);	// NAKs?

	// Test unit ready
	s2 = bulkcmd(0, 0, 0, null, 0);
	LOGV("bulkcmd returned 0x%08x\r\n", s2);
	assert(0 == s2);

	// Make an INQUIRY
	s3 = bulkcmd(0x12, 0, 0, buf, 36);
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
	s4 = bulkcmd(0x25, 0, 0, buf, 8);
	LOGV("bulkcmd returned 0x%08x\r\n", s4);
	assert(0 == s4);
	printf("Last Logical Block Address: 0x%08x\r\n", *(unsigned int *)&buf[0]);
	printf("Block Length in Bytes: 0x%08x\r\n", *(unsigned int *)&buf[4]);

	// Time to get down to it. Read in the first logical block of data:
	s5 = bulkcmd(0x28, 0, 1, buf, 0x200 /* XXX hard-coded block size */);
	LOGV("bulkcmd returned 0x%08x\r\n", s5);
	assert(0 == s5);

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
	haddr = 0x4001;
	while (1);
}

// Initialize the CY16 for Host USB on SIE1
short step1[] = {
	SIE1msg,	0,		// Clear SIE1msg
	//0xc090,	-1,		// Clear USB interrupts - Needs control register access
	0x1b4,		4800,	// HUSB_pEOT - EOT is 4800 bit times (unlikely)
	0x142, 		0x440,	// HPI intr routing reg: Don't put anything on HPI pin 
	//0xc0c8,	0x30,	// Hear about connect change events - Also a control reg
	LCP_INT,	0x72,	// HUSB_SIE1_INIT_INT - No regs, no return val
	0, 0				// End sequence.
};

// Force a RESET on SIE1
short step2[] = {
	LCP_R1,		0,		// r1 - Port number (0 = left, 1 = right port on skunk)
	LCP_R0,		0x3c,	// r0 - Reset duration in milliseconds
	LCP_INT,	0x74,	// Run HUSB_RESET_INT
	0, 0				// End sequence. Note: Caller to read return val in r0
};

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

// This does NOT currently handle Cruzer Mini 1GB correctly...
static int inithusb1(int port) {
	int s1, s2, s3, s4, s5, s6, s7;
	int i, j, k;
	char test[256], *p = test;
	char ascii[128];
	int manufStrIdx, prodStrIdx, serialStrIdx;
	int numconfigs, numfaces, numends, setconfig;
	int isbulk=0;
	char enSupported = 0;
	
	s1=s2=s3=s4=s5=s6=0;
	bulkin=bulkout=-1;
	
	// Turn on host USB ports (SIE1) 
	s1 = run(step1, 0xce01);	// Init HUSB on SIE1
	if (s1 != 0xfed) {
		return -1;
	}
	step2[1] = port;
	s2 = run(step2, 0xce01); 	// Reset SIE1 port 0
	if (s1 != 0xfed) {
		return -2;
	}

	// See if a device was found
	haddr = LCP_R0;
	i = hread;

	if (i & 0x2) {
		printf("No device connected\r\n");
		return -3;
	} else {
		// Pause to give the device some time to get ready after reset.
		// Some devices seem to need this, some don't.
		for (i = 0; i < 1000000; i++);
	}

	printf("%s-speed device detected on port %d\r\n",
		   (i & 1) ? "Low" : "Full", port);

	bulkdev = 2;
	s3 = usbctlmsg(0, 0, 5, bulkdev, 0, null, 0);	// Set USB device address to 2
	assert(0==s3);
	s4 = usbctlmsg(bulkdev, 0x80, 6, 0x100, 0, (char*)test, 18); 	// Read device descriptor
	assert(0==s4 && 1==test[1]);					// Make sure we got one
	numconfigs = test[17];
	
	if (0 == numconfigs) {		// Yay jumpdrive!  You are so broken...
		printf("Zero configs?!?\r\n");
		isbulk = 1;
		bulkin = 1;
		bulkout = 1;
	}

	// Get string language support
	s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300, 0, (char*)test, 1); 	// Read device string language list size
	if (!s7) {
		int size = test[0];
		const unsigned short *words = (const unsigned short *)&test[2];
		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300, 0, (char*)test, size); 	// Read device string language list

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
		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + manufStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s7) {
			int size = test[0];
			s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + manufStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s7 == 0);
			utf16letoascii(ascii, &test[2], size - 2);
			printf("Device Manufacturer:  %s\r\n", ascii);
		} else {
			printf("Failed to query device manufacturer");
		}

		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + prodStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s7) {
			int size = test[0];
			s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + prodStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s7 == 0);
			utf16letoascii(ascii, &test[2], size - 2);
			printf("Device Product Name:  %s\r\n", ascii);
		} else {
			printf("Failed to query device product name");
		}

		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + serialStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s7) {
			int size = test[0];
			s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + serialStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s7 == 0);
			utf16letoascii(ascii, &test[2], size - 2);
			printf("Device Serial Number: %s\r\n", ascii);
		} else {
			printf("Failed to query device serial number");
		}
	}
	
	// Parse all descriptors until we find the bulk interface
	for (i = 0; i < numconfigs && !isbulk; i++) {
		s5 = usbctlmsg(bulkdev, 0x80, 6, 0x200 + i, 0, (char*)test, 32);	// Read config descriptor
		assert(0==s5 && 2==test[1]);							// Make sure we got one
		
		p = test;			// Parse config packet
		numfaces = p[4];	// Number of interfaces for this config
		setconfig = p[5];	// Configuration number 
		p += p[0];			// Next descriptor
		
		for (j = 0; j < numfaces && !isbulk; j++) {	// Parse all interfaces
			assert(4==p[1]);	// Make sure we got one
			bulkface = p[2];	// Interface number
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
						bulkin = p[2]&0x7f;
					else
						bulkout = p[2]&0x7f;
					assert(p[4]==64);	// 64 byte packets, right?  Even for 2.0 devices?  ???
				}
				p += p[0];	// Next descriptor
			}
		}
	}
	
	// Make sure we really got them
	assert(isbulk && bulkin>0 && bulkout>0);

	LOGV("isbulk: %d bulkin: %d bulkout %d\r\n", isbulk, bulkin, bulkout);
	
	// Set our favorite configuration -- we are enumerated baby!
	s6 = usbctlmsg(bulkdev, 0, 9, setconfig, 0, null, 0);
	assert(0 == s6);

	return 0;
}

static int waitbulktdlist(int direction)
{
	int i, sie;

	// Wait for the result in SIE1 (but don't wait forever!)
	haddr = 0x4007;
	for (i = 100000; i && 0 == (hwrite & 16); i--)	;

	sie = 0xdead0000 | hwrite;

	LOGV("--bulk sie = 0x%08x, i = %d\r\n", sie, i);
	haddr = 0x4007;

	i = hwrite;
	if (i & 1) {		// HPI mailbox
		printf("** Got unexpected mailbox\r\n");
		haddr = 0x4005;
		i = hwrite;
		printf("** Mailbox: 0x%04x\r\n", i);
	}
	haddr = 0x4004;
	if (i & 32) {
		printf("** Got unexpected SIE2msg\r\n");
		haddr = 0x148;	// SIE2msg
		i = hread;
		printf("** Msg: 0x%04x\r\n", i);
	}

	if (i & 16) {
		haddr = 0x144;	// SIE1msg
		sie = hread;
		LOGV("-- SIE1 Message: 0x%04x\r\n", sie);
		haddr = 0x4004;

		if (sie == 0x1000) {	// OK so far, check for additional problems
			haddr = 0x1b6;
			i = hread;
			if (i != 1) {
				printf("** Got Done Message but HUSB_SIE_pTDListDoneSem not set!\r\n");
				haddr = 0x4004;
			}
			haddr = 0x1b6;
			hwrite = 0;

			haddr = 0x1506;		// First TD entry
			i = hreadd;
			LOGV("--Control = 0x%02x, status = 0x%02x, RetryCnt = 0x%02x, Residue = 0x%02x\r\n",
					(i >> 16) & 0xff, i >> 24, i & 0x0ff, (i >> 8) & 0xff);
			haddr = 0x4004;
			i &= 0xc6000010;
			if (0x40 == (i>>24)) {
				// Device NAKed.  Retry
				return 0xf33df33d;
			} else if (i != 0) {
				printf("Wait for Bulk TDList failed: i = 0x%08x\r\n", i);
				haddr = 0x4004;
				return i;
			}

			if (direction) {
				dToggleIn ^= 1;
			} else {
				dToggleOut ^= 1;
			}
			return 0;
		}
	}

	return sie;
}

/**
 * Executes a bulk mass storage command on bulkin/bulkout
 * Expects to read len bytes of data unless opcode = SCSI write
 * Do not exceed 64KB in len or 16M in blocknum or 256 in blockcount -- these are not decoded properly
 */
int bulkcmd(uchar opcode, int blocknum, int blockcount, char* buffer, int len)
{
	int i = 0, sie, retry;
	int direction = (opcode == 0xff) ? 0x0 : 0x80;	// 0 = write, 0x80 = read
	unsigned short t;
#define DT(a) (dToggle##a << 6)

	haddr = 0x4004;

	retry = 1;
	for (i = 0; i < 1000 && retry; i++) {
		retry = 0;
		haddr = 0x1500;			// TD list base

		// Build CBW
		hwrited = 0x150c001f;				// 0x1500: Command data is always 31 bytes located at 150c.
		hwrited = 0x00100001 | (bulkdev<<24) | (bulkout<<16) | DT(Out);	// 0x1504: 10 = OUT, 01 = ARM DATA0
		hwrited = 0x001b0000;				// 0x1508: <residue/pipe/retry> <end of TD list>
		hwrited = 0x53554342;				// 0x150c: CBW signature (43425355 in little endian land)
		hwrited = (seq >> 16)|(seq << 16);	// 0x1510: Command Block Tag (it's a sequence number)
		seq++;
		hwrited = len<<16;					// 0x1514: Transfer length (little endian)
		hwrite = direction;					// 0x1518: Direction + LUN (zero)
		hwrite = 0xc | (opcode<<8);			// 0x151a: We support the 12-byte USB Bootability SCSI subset
		switch (opcode) {
			case 0x28: // Read
				hwrited = (blocknum&0xff00) | (blocknum>>16);		// 0x151c: Middle-endian:  MSB=0, LUN=0, 1SB=>>8, 2SB=>>16
				hwrited = ((blocknum&0xff)<<16) | (blockcount<<8);	// 0x1520: Middle-endian:  Rsv=0, LSB=>>24, LSB, MSB
				hwrited = zero;						// 0x1524: Reserved fields
				hwrited = zero;						// 0x1528: Remainder of the CBW (unused with 12 byte commands)
				break;
			case 0x12: // Inquiry
				hwrite = 0;		// 0x151c: 2,1: LUN = 0, reserved byte = 0.
				hwrite = 0x24 << 8;	// 0x151e: 4,3: reserved byte = 0, allocation length = 0x24
				hwrite = zero;		// 0x1520: 6,5: reserved byte = 0, pad byte = 0
				hwrite = zero;		// 0x1522: 8,7: pad bytes = 0
				hwrite = zero;		// 0x1524: 10,9: pad bytes = 0
				hwrite = zero;		// 0x151e: 12,11: pad bytes = 0
				break;
			default:
				printf("Warning: Unhandled bulkcmd opcode: 0x%02x\r\n", opcode);
				haddr = 4004;
				haddr = 0x151c;
				/* fall through */
			case 0x00: // Test unit ready
				/* fall through */
			case 0x25:	// Read capacity
				hwrited = zero;				// 0x151c: LUN = 0, reserved
				hwrited = zero;				// reserved/pad
				hwrited = zero;				// reserved/pad
				hwrited = zero;				// unused.
				break;
		}

		hw(0x1b0, 0x1500);					// Execute our new TD

		LOGV("--Executing CBW\r\n");
		haddr = 0x4004;

		sie = waitbulktdlist(0);

		if (sie == 0xf33df33d) {
			retry = 1;
		}
	}

	if (sie != 0) {
		return sie;
	}
	
	// Build data section (if there is one)
	if (len) {
		int curMemAddr = 0x1600; // I/O data starts at 1600
		int remainder = len;
		int curChunk = 0;
		while (remainder > 0) {
			curChunk = (remainder >= 64) ? 64 : remainder;
			retry = 1;
			for (i = 0; i < 1000 && retry; i++) {
				retry = 0;
				haddr = 0x1500;			// TD list base
				hwrited = (curMemAddr<<16) | len;	// 0x1500
				if (direction)
					hwrited = 0x00900001 | (bulkdev<<24) | (bulkin<<16) | DT(In); // 90 = IN, 01 = ARM DATA0
				else
					hwrited = 0x00100001 | (bulkdev<<24) | (bulkout<<16) | DT(Out); // 10 = OUT, 01 = ARM DATA0
				hwrited = 0x001b0000;		// 0x1508: <residue/pipe/retry> <end of TD list>

				hw(0x1b0, 0x1500);					// Execute our new TD

				LOGV("--Executing Data %s\r\n", direction ? "IN" : "OUT");
				haddr = 0x4004;

				sie = waitbulktdlist(direction);

				if (sie == 0xf33df33d) {
					retry = 1;
				}
			}

			if (sie != 0) {
				return sie;
			}
			remainder -= curChunk;
			curMemAddr += curChunk;
		}
	}
	
	retry = 1;
	for (i = 0; i < 1000 && retry; i++) {
		retry = 0;
		haddr = 0x1500;			// TD list base

		// Build CSW check
		hwrited = 0x1544000d;				// 0x1500: Status data is always 13 bytes located at 1544.
		hwrited = 0x00900001 | (bulkdev<<24) | (bulkin<<16) | DT(In);	// 0x1508: 90 = IN, 01 = ARM DATA0
		hwrited = 0x001b0000;				// 0x1508: <residue/pipe/retry> <end of TD list>

		hw(0x1b0, 0x1500);					// Execute our new TD

		LOGV("--Executing CSW\r\n");
		haddr = 0x4004;

		sie = waitbulktdlist(0x80);

		if (sie == 0xf33df33d) {
			retry = 1;
		}
	}

	if (sie != 0) {
		return sie;
	}

	if ((direction & 0x80) && len) {				// We have data to read
		haddr = 0x1600;
		while (len >= 0) {
			t = hread;
			buffer[0] = t;				// Endian swap
			buffer[1] = t >> 8;
			buffer += 2;
			len -= 2;
		}
	}

	haddr = 0x1544;
	i = hreadd;
	if (i != 0x53555342) {
		printf("Invalid CSW signature: 0x%08x\r\n", i);
		haddr = 0x4004;
		haddr = 0x1548;
	}
	i = hreadd;
	i = (i << 16) | (i >> 16);
	if (i != seq - 1) {
		printf("Invalid CSW tag: 0x%08x\r\n", i);
		printf("      Should be: 0x%08x\r\n", seq - 1);
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
static int usbctlmsg(uchar dev, uchar rtype, uchar request, short value, short index, char* buffer, short len)
{
	short t;
	int i = 0, sie, olen = len;
	
	// Construct a TD list for a control message based on the parameters provided
	haddr = 0x4004;
	haddr = 0x1500;			// TD list base
	
	hwrited = 0x150c0008;				// Setup data is always 8 bytes located 150c.
	hwrited = 0x00D00001 | (dev<<24);	// D0 = 'setup', control is always EP 0, 01 = ARM DATA0
	hwrited = 0x00131514;				// <residue/pipe/retry> <start of next TD>
	hwrite = (rtype) | (request<<8);	// Setup data:  request (MSB), rtype (LSB)
	hwrite = value;
	hwrite = index;
	hwrite = len;
	
	if (len) {							// If we have data to move, add a data phase
		hwrite = 0x152c;				// Default base address for data transfers
		hwrite = len;
		hwrite = 0x10 | (dev<<8) | (rtype & 0x80);	// 10 = OUT, 90 = IN (based on rtype)
		hwrite = 0x41;					// ARM, DATA1 phase -- setup is DATA0, first data packet is DATA1, next data packet is DATA0, etc. Status is always DATA1
		hwrited = 0x00131520;			// <residue/pipe/retry> <start of next TD>
	}
	
	hwrited = i;						// Status phase has no data (should be 00, i avoids clr bug)  
	hwrite = 0x10 | (dev<<8) | (~rtype & 0x80);		// Opposite of rtype (ACK OUTs with IN, INs with OUT)
	hwrite = 0x41;						// ARM, STATUS is always DATA1 phase
	hwrited = 0x00130000;				// <residue/pipe/retry> <end of TD list>
	
	if (!(rtype & 0x80))				// We have data to write
		while (len >= 0) {			
			hwrite = buffer[0] | (buffer[1]<<8);  // Endian swap
			buffer += 2;
			len -= 2;
		}

	hw(0x1b0, 0x1500);					// Execute our new TD
	
	// Wait for the result in SIE1 (but don't wait forever!)
	haddr = 0x4007;
	for (i = 100000; i && 0 == (hwrite & 16); i--)	;

	sie = 0xdead0000 | hwrite;

	LOGV("reading sie, i = %d, sie = 0x%08x\r\n", i, sie);
	haddr = 0x4007;
	
	i = hwrite;
	if (i & 1) {		// HPI mailbox
		haddr = 0x4005;
		i = hwrite;
	}
	haddr = 0x4004;
	if (i & 32) {
		haddr = 0x148;	// SIE2msg
		i = hread;
	}

	if (i & 16) {
		haddr = 0x144;	// SIE1msg
		sie = hread;
		if (sie == 0x1000) {	// OK so far, check for additional problems
			haddr = 0x1506;		// First TD entry
			i = hreadd & 0xc6000010;
			if (i != 0)
				return i;

			haddr = 0x151a;		// Second TD entry
			i = hreadd & 0xc6000010;
			if (i != 0)
				return i;
			
			if (olen) {
				haddr = 0x1526;		// Third TD entry
				i = hreadd & 0xc6000010;
				if (i != 0)
					return i;
				
				if (rtype & 0x80) {				// We have data to read
					haddr = 0x152c;
					while (len >= 0) {
						t = hread;
						buffer[0] = t;				// Endian swap
						buffer[1] = t >> 8;
						buffer += 2;
						len -= 2;
					}
				}
			}
			
			return 0;
		}
	}
	
	return sie;		
}

static int run(short* p, short mbox) {
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
