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

COMMONGFXOBJS = ffsgpu.o ffsobj.o blitcode.o

GFXOBJS = testgfx.o

FFSSTART = startffs.o
FFSOBJS = ffs/ff.o \
	ffs/ffunicode.o \
	usbffs.o \
	string.o \
	skunk.o \
	dspjoy.o \
	flash.o

OBJS = $(COMMONOBJS) \
	$(COMMONGFXOBJS) \
	$(COMMONSTART) \
	$(TESTOBJS) \
	$(DUMPOBJS) \
	$(FFSOBJS) \
	$(FFSSTART) \
	$(GFXOBJS)

TESTCOF = testdrv.cof
DUMPCOF = usbdump.cof
VERIFCOF = usbverif.cof
FFSCOF = usbffs.cof
GFXCOF = testgfx.cof

include $(JAGSDK)/jaguar/skunk/skunk.mk

PROGS = $(TESTCOF) $(DUMPCOF) $(VERIFCOF) $(FFSCOF) $(GFXCOF)

$(TESTCOF): $(COMMONSTART) $(COMMONOBJS) $(TESTOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(DUMPCOF): $(COMMONSTART) $(COMMONOBJS) $(DUMPOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(VERIFCOF): $(COMMONSTART) $(COMMONOBJS) $(VERIFOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(FFSCOF): $(FFSSTART) $(FFSOBJS) $(COMMONGFXOBJS) $(COMMONOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(GFXCOF): $(FFSSTART) $(GFXOBJS) $(COMMONGFXOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

skunkc.o: $(SKUNKDIR)/lib/skunkc.s
	$(ASM) $(ASMFLAGS) -o $@ $<

.PHONY: run
run:	$(COF)
	rjcp libre -c $(COF)

include $(JAGSDK)/tools/build/jagrules.mk
