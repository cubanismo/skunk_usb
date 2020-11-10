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

#define		assert(_x)	{ if (!(_x)) { printf(#_x "\r\n"); asm("trap #2"); } } 

extern int printf(const char *fmt, ...);

void inithusb1();
int run(short* p, short mbox);
int usbctlmsg(uchar dev, uchar rtype, uchar request, short value, short index, char* buffer, short len);
int bulkcmd(uchar opcode, int blocknum, int blockcount, char* buffer, int len);

int bulkin, bulkout, bulkface, bulkdev;
int seq = 0xf1f1bab5, zero = 0;

void start() {
	char buf[2048];
	int s1, s2, s3;

	printf("Staring up\r\n");
	
	zero = 0;		// Until BSS starts working
	
	// General initialization and enumeration
	inithusb1();

	printf("USB1 Initialized\r\n");

	// Test unit ready
	//s1 = usbctlmsg(bulkdev, 0x21, 0xff, 0, bulkface, null, 0);	// Reset the device
	//assert(0 == s1);	// NAKs?
	s2 = bulkcmd(0, 0, 0, null, 0);
	printf("bulkcmd returned 0x%08x\r\n", s2);
	assert(0 == s2);
	// Get disk size and block size
	//assert(0 == bulkcmd(0x25, 0, 0, buf, 8));
	
	printf("Done!\r\n");
	// There's nowhere for us to return!
	asm("trap #1");
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

// This does NOT currently handle Cruzer Mini 1GB correctly...
void inithusb1() {
	int s1, s2, s3, s4, s5, s6;
	int i, j, k;
	char test[256], *p = test;
	int numconfigs, numfaces, numends, setconfig;
	int isbulk=0;
	
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
	hwrited = 0x150c001f;				// Command data is always 31 bytes located at 150c.
	hwrited = 0x00100041 | (bulkdev<<24) | (bulkout<<16);	// 10 = OUT, 1 means 'ARM DATA1'
	hwrited = 0x0013152c;				// <residue/pipe/retry> <start of next TD>
	hwrited = 0x53554342;				// CBW signature (43425355 in little endian land)
	hwrited = seq;						// Command Block Tag (it's a sequence number)
	hwrited = len<<16;					// Transfer length (little endian)
	hwrite = direction;					// Direction + LUN (zero)
	hwrite = 0xc | (opcode<<8);			// We support the 12-byte USB Bootability SCSI subset
	hwrited = (blocknum&0xff00) | (blocknum>>16);		// Middle-endian:  MSB=0, LUN=0, 1SB=>>8, 2SB=>>16
	hwrited = ((blocknum&0xff)<<16) | (blockcount<<8);	// Middle-endian:  Rsv=0, LSB=>>24, LSB, MSB
	hwrited = zero;						// Reserved fields
	hwrited = zero;						// Remainder of the CBW (unused with 12 byte commands)
	
	// Build data section (if there is one)
	if (len) {
		hwrited = 0x16000000 | len;		// I/O data starts at 1600
		if (direction)
			hwrited = 0x00900041 | (bulkdev<<24) | (bulkin<<16);	// 90 = IN, 1 means 'ARM DATA1'
		else
			hwrited = 0x00100041 | (bulkdev<<24) | (bulkout<<16);	// 10 = OUT, 1 means 'ARM DATA1'
		hwrited = 0x00131538;				// <residue/pipe/retry> <start of next TD>
	}
	
	// Build CSW check
	hwrited = 0x1544000d;				// Status data is always 13 bytes located at 1544.
	hwrited = 0x00900041 | (bulkdev<<24) | (bulkin<<16);	// 90 = IN, 1 means 'ARM DATA1'
	hwrited = 0x00130000;				// <residue/pipe/retry> <end of TD list>
	
	hw(0x1b0, 0x1500);					// Execute our new TD
	
	retry = 1;
	for (j = 0; j < 1000 && retry; j++) {
		retry = 0;
		// Wait for the result in SIE1 (but don't wait forever!)
		haddr = 0x4007;
		for (i = 100000; i && 0 == (hwrite & 16); i--)	;
	
		sie = 0xdead0000 | hwrite;

		//printf("bulk sie = 0x%08x\r\n", sie);
		//haddr = 0x4007;
		
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
				i = hreadd & 0xc6000010;
				if (0x40 == (i>>24)) {
					// Build CSW check
					haddr = 0x152c;
					hwrited = 0x1544000d;				// Status data is always 13 bytes located at 1544.
					hwrited = 0x00900041 | (bulkdev<<24) | (bulkin<<16);	// 90 = IN, 1 means 'ARM DATA1'
					hwrited = 0x00130000;				// <residue/pipe/retry> <end of TD list>
					
					hw(0x1b0, 0x152c);					// Execute our new TD
					retry = 1;
					printf("Executing new TD\r\n");
					haddr = 0x4004;
					continue;
				}
				if (i != 0) {
					printf("Uhoh, i = 0x%08x\r\n", i);
					haddr = 0x4004;
					return i;
				}
				
				if (olen) {
					haddr = 0x153e;		// Second TD entry
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
				
				return 0;
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
	hwrited = 0x00d00001 | (dev<<24);	// d0 = 'setup', 1 means 'ARM', control is always EP 0
	hwrited = 0x00131514;				// <residue/pipe/retry> <start of next TD>
	hwrite = (rtype) | (request<<8);	// Setup data:  request (MSB), rtype (LSB)
	hwrite = value;
	hwrite = index;
	hwrite = len;
	
	if (len) {							// If we have data to move, add a data phase
		hwrite = 0x152c;				// Default base address for data transfers
		hwrite = len;
		hwrite = 0x10 | (dev<<8) | (rtype & 0x80);	// 10 = OUT, 90 = IN (based on rtype)
		hwrite = 0x41;					// DATA1 phase -- for some reason
		hwrited = 0x00131520;			// <residue/pipe/retry> <start of next TD>
	}
	
	hwrited = i;						// Status phase has no data (should be 00, i avoids clr bug)  
	hwrite = 0x10 | (dev<<8) | (~rtype & 0x80);		// Opposite of rtype (ACK OUTs with IN, INs with OUT)
	hwrite = 0x41;						// DATA1 phase
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
				haddr = 0x1526;		// Second TD entry
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
