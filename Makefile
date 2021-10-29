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

LOADEROBJS = loader.o blitcode.o

OBJS = $(COMMONOBJS) \
	$(COMMONGFXOBJS) \
	$(COMMONSTART) \
	$(TESTOBJS) \
	$(DUMPOBJS) \
	$(FFSOBJS) \
	$(FFSSTART) \
	$(GFXOBJS) \
	$(LOADEROBJS)

TESTCOF = testdrv.cof
DUMPCOF = usbdump.cof
VERIFCOF = usbverif.cof
FFSCOF = usbffs.cof
FFSBIN = usbffs.bin
GFXCOF = testgfx.cof
LOADERBIN = loader.bin
FFSROM = usbffs.j64

ifeq ($(SKUNKLIB),1)
	include $(JAGSDK)/jaguar/skunk/skunk.mk
endif

PROGS = $(FFSCOF) $(FFSBIN) $(FFSROM) $(GFXCOF) $(LOADERBIN)

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

FFSDEPS = $(FFSSTART) $(FFSOBJS) $(COMMONGFXOBJS) $(COMMONOBJS)

$(FFSCOF): $(FFSDEPS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(FFSBIN): $(FFSDEPS)
	$(LINK) -n -r$(ALIGN) -a $(STADDR) x $(BSSADDR) -o $@ $^

$(GFXCOF): $(FFSSTART) $(GFXOBJS) $(COMMONGFXOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

# Note it doesn't matter what STADDR or BSSADDR are here: The loader code is all
# position-independent and doesn't have a bss or data section.
$(LOADERBIN): $(LOADEROBJS)
	$(LINK) -n -r$(ALIGN) -a $(STADDR) x $(BSSADDR) -o $@ $^

$(FFSROM): $(FFSBIN) $(LOADERBIN)
	truncate -s 1028 $@	# Create Fake ROM Header: 1028 of junk
	echo -e "\x00\x80\x20\x00" >> $@	# Fake start addr: 0x802000
	truncate -s 8k $@	# Create Fake ROM Header: The rest
	cat $(LOADERBIN) >> $@	# Append a copy of loader.bin (First boot)
	truncate -s 8448 $@	# Pad up to 8k + 256B
	cat $(FFSBIN) >> $@	# Append a copy of usbffs
	truncate -s 2MiB $@	# Pad up to 2MiB
	cat $(LOADERBIN) >> $@	# Append another copy of loader.bin (autoboot)
	truncate -s 2097408 $@	# Pad up to 2MiB + 256B
	cat $(FFSBIN) >> $@	# Append another copy of usbffs
	truncate -s 4194288 $@	# Pad up to 4MiB - 16B
	# Add Autoboot Vector: "LION" + <flag byte> + <3 address bytes>
	# flag bytes: 0x00 == Boot with bank 0 selected
	#             0x10 == Boot with bank 1 selected
	#             0x70 == Boot in 6MiB mode
	echo -e "LION\x10\xA0\x00\x00" >> $@
	truncate -s 4M $@	# Pad up to 4MiB

skunkc.o: $(SKUNKDIR)/lib/skunkc.s
	$(ASM) $(ASMFLAGS) -o $@ $<

.PHONY: run
run:	$(COF)
	rjcp libre -c $(COF)

include $(JAGSDK)/tools/build/jagrules.mk
