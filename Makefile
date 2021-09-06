ALIGN=q
include $(JAGSDK)/tools/build/jagdefs.mk

CFLAGS += -Wall -fno-builtin -I.

COMMONOBJS = startup.o \
	usb.o \
	sprintf.o \
	skunkc.o \
	util.o

TESTOBJS = testdrv.o skunk.o

DUMPOBJS = usbdump.o skunk.o

VERIFOBJS = usbverif.o skunk.o

FFSOBJS = ffs/ff.o ffs/ffunicode.o usbffs.o string.o skunk.o flash.o

OBJS = $(COMMONOBJS) $(TESTOBJS) $(DUMPOBJS) $(FFSOBJS)

TESTCOF = testdrv.cof
DUMPCOF = usbdump.cof
VERIFCOF = usbverif.cof
FFSCOF = usbffs.cof

include $(JAGSDK)/jaguar/skunk/skunk.mk

PROGS = $(TESTCOF) $(DUMPCOF) $(VERIFCOF) $(FFSCOF)

$(TESTCOF): $(COMMONOBJS) $(TESTOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(DUMPCOF): $(COMMONOBJS) $(DUMPOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(VERIFCOF): $(COMMONOBJS) $(VERIFOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(FFSCOF): $(COMMONOBJS) $(FFSOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

skunkc.o: $(SKUNKDIR)/lib/skunkc.s
	$(ASM) $(ASMFLAGS) -o $@ $<

.PHONY: run
run:	$(COF)
	rjcp libre -c $(COF)

include $(JAGSDK)/tools/build/jagrules.mk
