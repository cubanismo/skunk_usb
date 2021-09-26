ALIGN=q
include $(JAGSDK)/tools/build/jagdefs.mk

CFLAGS += -Wall -fno-builtin -I.

COMMONSTART = startup.o
COMMONOBJS = usb.o \
	sprintf.o \
	skunkc.o \
	util.o

TESTOBJS = testdrv.o skunk.o

DUMPOBJS = usbdump.o skunk.o

VERIFOBJS = usbverif.o skunk.o

FFSSTART = startffs.o
FFSOBJS = ffsobj.o \
	ffs/ff.o \
	ffs/ffunicode.o \
	usbffs.o \
	string.o \
	skunk.o \
	flash.o \
	ffsgpu.o

OBJS = $(COMMONOBJS) \
	$(COMMONSTART) \
	$(TESTOBJS) \
	$(DUMPOBJS) \
	$(FFSOBJS) \
	$(FFSSTART)

TESTCOF = testdrv.cof
DUMPCOF = usbdump.cof
VERIFCOF = usbverif.cof
FFSCOF = usbffs.cof

include $(JAGSDK)/jaguar/skunk/skunk.mk

PROGS = $(TESTCOF) $(DUMPCOF) $(VERIFCOF) $(FFSCOF)

$(TESTCOF): $(COMMONSTART) $(COMMONOBJS) $(TESTOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(DUMPCOF): $(COMMONSTART) $(COMMONOBJS) $(DUMPOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(VERIFCOF): $(COMMONSTART) $(COMMONOBJS) $(VERIFOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(FFSCOF): $(FFSSTART) $(FFSOBJS) $(COMMONOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

skunkc.o: $(SKUNKDIR)/lib/skunkc.s
	$(ASM) $(ASMFLAGS) -o $@ $<

.PHONY: run
run:	$(COF)
	rjcp libre -c $(COF)

include $(JAGSDK)/tools/build/jagrules.mk
