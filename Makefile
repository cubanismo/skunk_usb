ALIGN=q
include $(JAGSDK)/tools/build/jagdefs.mk

# Change this to 0 to build without the skunk console dependency.
# Programs that won't work without the console won't be built.
# This is most useful for building the stand-alone version of usbffs.
SKUNKLIB := 1

CFLAGS += -Wall -fno-builtin -I.

COMMONSTART = startup.o
COMMONOBJS = usb.o \
	sprintf.o \
	util.o

ifeq ($(SKUNKLIB),1)
	COMMONOBJS += skunkc.o skunk.o
endif

TESTOBJS = testdrv.o

DUMPOBJS = usbdump.o

VERIFOBJS = usbverif.o

COMMONGFXOBJS = ffsgpu.o ffsobj.o blitcode.o

GFXOBJS = testgfx.o

FFSSTART = startffs.o
FFSOBJS = ffs/ff.o \
	ffs/ffunicode.o \
	usbffs.o \
	string.o \
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

ifeq ($(SKUNKLIB),1)
	include $(JAGSDK)/jaguar/skunk/skunk.mk
endif

PROGS = $(FFSCOF) $(GFXCOF)

ifeq ($(SKUNKLIB),1)
	# These programs require the skunk console to work.
	PROGS += $(TESTCOF) $(DUMPCOF) $(VERIFCOF)
endif

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
