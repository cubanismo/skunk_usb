// Test file system access using HPI mode on Jaguar
// Next step -- create a usbctlmsg() based on TD lists

#define		haddr		((volatile short*)0xC00000)[0]
#define		hread		((volatile short*)0xC00000)[0]
#define		hreadd		((volatile int*)0xC00000)[0]
#define		hwrite		((volatile short*)0x800000)[0]
#define		hwrited		((volatile int*)0x800000)[0]
#define		hboxw(_x)	{haddr=0x4005; hwrite=(_x); haddr=0x4004;}
#define		hw(_a, _x)	{haddr=(_a); hwrite=(_x);}
#define		checkbox()	{haddr=0x4005; result=hread; haddr=0x4004;}

#define		null		0
#define 	uchar		unsigned char

//#define		assert(_x)	{ if (!(_x)) { printf(#_x "\r\n"); asm("trap #2"); } } 
#define		assert(_x)	{ if (!(_x)) { printf(#_x "\r\n"); while (1); } } 

extern int printf(const char *fmt, ...);

#include "skunk.h"

void inithusb1();
int run(short* p, short mbox);
int usbctlmsg(uchar dev, uchar rtype, uchar request, short value, short index, char* buffer, short len);
int bulkcmd(uchar opcode, int blocknum, int blockcount, char* buffer, int len);

int bulkin, bulkout, bulkface, bulkdev;
int seq = 0x00000001;
int zero = 0;

void start() {
	char buf[2048];
	int s1, s2, s3, s4;

	skunkRESET();
	skunkNOP();
	skunkNOP();

	printf("Staring up\r\n");
	
	zero = 0;		// Until BSS starts working
	
	// General initialization and enumeration
	inithusb1();

	printf("USB1 Initialized\r\n");

	s1 = usbctlmsg(bulkdev, 0x21, 0xff, 0, bulkface, null, 0);	// Reset the device
	assert(0 == s1);	// NAKs?

	// Test unit ready
	s2 = bulkcmd(0, 0, 0, null, 0);
	printf("bulkcmd returned 0x%08x\r\n", s2);
	assert(0 == s2);

	// Make an INQUIRY
	s3 = bulkcmd(0x12, 0, 0, buf, 36);
	printf("bulkcmd returned 0x%08x\r\n", s3);
	assert(0 == s3);

	// Get disk size and block size
	s4 = bulkcmd(0x25, 0, 0, buf, 8);
	printf("bulkcmd returned 0x%08x\r\n", s4);
	assert(0 == s4);
	
	printf("Done!\r\n");
	// There's nowhere for us to return!
	haddr = 0x4001;
	while (1);
}

// Initialize the CY16 for Host USB on SIE1
short step1[] = {
	0x144, 0,		// Clear SIE1msg
	//0xc090, -1,		// Clear USB interrupts - this kills the EZ-HOST -- DOH!  We can't set in this range...
	0x1b4, 4800,	// EOT is 4800 bit times (unlikely)
	0x142, 0x440,	// Don't put anything on HPI pin 
	//0xc0c8, 0x30,	// Find out about connect change events -- can't set this high either...			
	0x1c2, 0x72,	// HUSB_SIE1_INIT_INT
	0, 0
};

// Force a RESET on SIE1
short step2[] = {
	0x1c2, 0x74,	// Run HUSB_RESET_INT
	0x1c4, 0x3c,	// Duration?
	0x1c6, 0,		// Port number (0 = keystick port)
	0, 0
};

// Convert a USB-style 16-bit-word Unicode string to ascii
static void utf16toascii(char *out, const char *in, int length)
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
void inithusb1() {
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
	printf("s1 = 0x%08x\r\n", s1);
	haddr=0x4004;
	s2 = run(step2, 0xce01); 	// Reset SIE1 port 0
	printf("s2 = 0x%08x\r\n", s2);
	haddr=0x4004;

	bulkdev = 2;
	s3 = usbctlmsg(0, 0, 5, bulkdev, 0, null, 0);			// Set USB device address to 2
	printf("s3 = 0x%08x\r\n", s3);
	haddr = 0x4004;
	assert(0==s3);
	s4 = usbctlmsg(bulkdev, 0x80, 6, 0x100, 0, (char*)test, 18); 	// Read device descriptor
	assert(0==s4 && 1==test[1]);						// Make sure we got one
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

		printf("Language IDs supported:\n");
		for (i = 0; i < (test[0] - 2); i++ ) {
			// Returned in little-endian, so swap:
			unsigned short langID = (words[i] << 8) | (words[i] >> 8);
			printf("    0x%04x\n", langID);
			if (langID == 0x409) // English (United States)
				enSupported = 1;
		}
	}

	if (enSupported) {
		// Print out some device info strings
		manufStrIdx = test[14];
		prodStrIdx = test[15];
		serialStrIdx = test[16];
		printf("Querying Device Manufacturer, string idx %d\n", manufStrIdx);
		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + manufStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s7) {
			int size = test[0];
			s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + manufStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s7 == 0);
			utf16toascii(ascii, &test[2], size - 2);
			printf("Device Manufacturer: %s\n", ascii);
		} else {
			printf("Failed to query device manufacturer");
		}

		printf("Querying Device Product Name, string idx %d\n", prodStrIdx);
		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + prodStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s7) {
			int size = test[0];
			s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + prodStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s7 == 0);
			utf16toascii(ascii, &test[2], size - 2);
			printf("Device Product Name: %s\n", ascii);
		} else {
			printf("Failed to query device product name");
		}

		printf("Querying Device Serial Number, string idx %d\n", serialStrIdx);
		s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + serialStrIdx,
					   0x904 /* 0x409 byte swapped */, (char*)test, 2);
		if (!s7) {
			int size = test[0];
			s7 = usbctlmsg(bulkdev, 0x80, 6, 0x300 + serialStrIdx,
						   0x904 /* 0x409 byte swapped */, (char*)test, size);
			assert(s7 == 0);
			utf16toascii(ascii, &test[2], size - 2);
			printf("Device Serial Number: %s\n", ascii);
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

	printf("isbulk: %d bulkin: %d bulkout %d\r\n", isbulk, bulkin, bulkout);
	
	// Set our favorite configuration -- we are enumerated baby!
	s6 = usbctlmsg(bulkdev, 0, 9, setconfig, 0, null, 0);
	assert(0 == s6);
}

/**
 * Executes a bulk mass storage command on bulkin/bulkout
 * Expects to read len bytes of data unless opcode = SCSI write
 * Do not exceed 64KB in len or 16M in blocknum or 256 in blockcount -- these are not decoded properly
 */
int bulkcmd(uchar opcode, int blocknum, int blockcount, char* buffer, int len)
{
	int i = 0, sie, olen = len, j, retry;
	int direction = (opcode == 0xff) ? 0x0 : 0x80;	// 0 = write, 0x80 = read
	haddr = 0x4004;
	haddr = 0x1500;			// TD list base

	// Build CBW
	hwrited = 0x150c001f;				// 0x1500: Command data is always 31 bytes located at 150c.
	hwrited = 0x00100001 | (bulkdev<<24) | (bulkout<<16);	// 0x1504: 10 = OUT, 01 = ARM DATA0
	hwrited = 0x0013152c;				// 0x1508: <residue/pipe/retry> <start of next TD>
	hwrited = 0x53554342;				// 0x150c: CBW signature (43425355 in little endian land)
	hwrited = (seq >> 16)|(seq << 16);	// 0x1510: Command Block Tag (it's a sequence number)
	seq++;
	hwrited = len<<16;					// 0x1514: Transfer length (little endian)
	hwrite = direction;					// 0x1518: Direction + LUN (zero)
	hwrite = 0xc | (opcode<<8);			// 0x151a: We support the 12-byte USB Bootability SCSI subset
	hwrited = (blocknum&0xff00) | (blocknum>>16);		// 0x151c: Middle-endian:  MSB=0, LUN=0, 1SB=>>8, 2SB=>>16
	hwrited = ((blocknum&0xff)<<16) | (blockcount<<8);	// 0x1520: Middle-endian:  Rsv=0, LSB=>>24, LSB, MSB
	hwrited = zero;						// 0x1524: Reserved fields
	hwrited = zero;						// 0x1528: Remainder of the CBW (unused with 12 byte commands)
	
	// Build data section (if there is one)
	if (len) {
		hwrited = 0x16000000 | len;		// 0x152c: I/O data starts at 1600
		if (direction)
			hwrited = 0x00900001 | (bulkdev<<24) | (bulkin<<16); // 90 = IN, 01 = ARM DATA0
		else
			hwrited = 0x00100001 | (bulkdev<<24) | (bulkout<<16); // 10 = OUT, 01 = ARM DATA0
		hwrited = 0x00131538;				// 0x1534: <residue/pipe/retry> <start of next TD>
	}
	
	// Build CSW check
	hwrited = 0x1544000d;				// 0x152c/1538: Status data is always 13 bytes located at 1544.
	hwrited = 0x00900001 | (bulkdev<<24) | (bulkin<<16);	// 0x1530/153c: 90 = IN, 01 = ARM DATA0
	hwrited = 0x00130000;				// 0x1534/1540: <residue/pipe/retry> <end of TD list>
	
	hw(0x1b0, 0x1500);					// Execute our new TD
	
	retry = 1;
	for (j = 0; j < 1000 && retry; j++) {
		retry = 0;
		// Wait for the result in SIE1 (but don't wait forever!)
		haddr = 0x4007;
		for (i = 100000; i && 0 == (hwrite & 16); i--)	;
	
		sie = 0xdead0000 | hwrite;

		printf("--bulk sie = 0x%08x, i = %d\r\n", sie, i);
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
				if (i != 0) {
					printf("First bad, i = 0x%08x\r\n", i);
					haddr = 0x4004;
					return i;
				}
	
				haddr = 0x1532;		// Second TD entry
				i = hreadd;

				printf("--Control = 0x%02x, status = 0x%02x, RetryCnt = 0x%02x, Residue = 0x%02x\r\n",
				(i >> 16) & 0xff, i >> 24, i & 0x0ff, (i >> 8) & 0xff);
				haddr = 0x4004;

				i &= 0xc6000010;
				if (0x40 == (i>>24)) {
					// Build data section (if there is one)
					haddr = 0x152c;
					if (len) {
						hwrited = 0x16000000 | len;		// 0x152c: I/O data starts at 1600
						if (direction)
							hwrited = 0x00900001 | (bulkdev<<24) | (bulkin<<16); // 01 = ARM DATA0
						else
							hwrited = 0x00100001 | (bulkdev<<24) | (bulkout<<16); // 01 = ARM DATA0
						hwrited = 0x00131538;			// 0x1534: <residue/pipe/retry> <start of next TD>
					}

					// Build CSW check
					hwrited = 0x1544000d;				// Status data is always 13 bytes located at 1544.
					hwrited = 0x00900001 | (bulkdev<<24) | (bulkin<<16);	// 90 = IN, 01 = ARM DATA0
					hwrited = 0x00130000;				// <residue/pipe/retry> <end of TD list>
					
					hw(0x1b0, 0x152c);					// Execute our new TD
					retry = 1;
					printf("--Executing new CSW TD\r\n");
					haddr = 0x4004;
					continue;
				}
				if (i != 0) {
					printf("Uhoh, i = 0x%08x\r\n", i);
					haddr = 0x4004;
					return i;
				}
				
				if (olen) {
					haddr = 0x153e;		// Third TD entry
					i = hreadd & 0xc6000010;
					if (i != 0) {
						printf("Uhoh Two, i = 0x%08x\r\n", i);
						haddr = 0x4004;
						return i;
					}
	/*				
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
					*/
				}

				haddr = 0x1544;
				i = hreadd;
				if (i != 0x53555342) {
					printf("Invalid CSW signature: 0x%08x\n", i);
					haddr = 0x4004;
					haddr = 0x1548;
				}
				i = hreadd;
				i = (i << 16) | (i >> 16);
				if (i != seq - 1) {
					printf("Invalid CSW tag: 0x%08x\n", i);
					printf("      Should be: 0x%08x\n", seq - 1);
					haddr = 0x4004;
					haddr = 0x154c;
				}
				i = hreadd;
				i = (i >> 16) | (i << 16);
				if (i != 0) {
					printf("CSW Residue: 0x%08x\n", i);
					haddr = 0x4004;
					haddr = 0x1550;
				}
				i = hread;
				i &= 0xff;
				if (i != 0) {
					printf("CSW status indicates failure: %d\n", i);
				}
				
				return i;
			}
		}
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
int usbctlmsg(uchar dev, uchar rtype, uchar request, short value, short index, char* buffer, short len)
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

	printf("reading sie, i = %d, sie = 0x%08x\n", i, sie);
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

int run(short* p, short mbox) {
	int i, mb, sie;
	
	haddr = 0x4004;
	while (*p != 0) {
		haddr = *p++;
		hwrite = *p++;
	}
	
	if (0 != mbox)
		hboxw(mbox);	  	
	
	// Wait for the result in HPI status register
	haddr = 0x4007;
	for (i = 100000; i && 0 == (hwrite & 17); i--)	;

	mb = sie = 0xdead0000 | hwrite;
	printf("i = %d, mb = 0x%08x sie = 0x%08x\r\n", i, mb, sie);
	haddr = 0x4007;
	
	i = hwrite;
	printf("Now i = 0x%08x\r\n", i);
	haddr = 0x4007;
	if (i & 1) {		// HPI mailbox
		haddr=0x4005;
		mb=hwrite;
		printf("Got MB: Now = 0x%08x\r\n", mb);
		haddr=0x4005;
	}
	
	haddr=0x4004;
	if (i & 16) {
		haddr=0x144;		// SIE1msg
		sie=hread;
		printf("Got SIE1msg: sie1 = 0x%08x\r\n", sie);
		haddr=0x4004;
	}
	if (i & 32) {
		haddr=0x148;		// SIE2msg
		i=hread;
		printf("Got SIE2msg: sie2 = 0x%08x\r\n", i);
		haddr=0x4004;
	}

	return (0 == mbox) ? sie : mb;		
}
