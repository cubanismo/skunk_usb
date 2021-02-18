ALIGN=q
include $(JAGSDK)/tools/build/jagdefs.mk

CFLAGS += -Wall -fno-builtin

COMMONOBJS = startup.o \
	usb.o \
	sprintf.o \
	skunkc.o \
	util.o

TESTOBJS = testdrv.o skunk.o

DUMPOBJS = usbdump.o skunk.o

VERIFOBJS = usbverif.o skunk.o

OBJS = $(COMMONOBJS) $(TESTOBJS) $(DUMPOBJS)

TESTCOF = testdrv.cof
DUMPCOF = usbdump.cof
VERIFCOF = usbverif.cof

include $(JAGSDK)/jaguar/skunk/skunk.mk

PROGS = $(TESTCOF) $(DUMPCOF) $(VERIFCOF)

$(TESTCOF): $(COMMONOBJS) $(TESTOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(DUMPCOF): $(COMMONOBJS) $(DUMPOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

$(VERIFCOF): $(COMMONOBJS) $(VERIFOBJS)
	$(LINK) $(LINKFLAGS) -o $@ $^

skunkc.o: $(SKUNKDIR)/lib/skunkc.s
	$(ASM) $(ASMFLAGS) -o $@ $<

.PHONY: run
run:	$(COF)
	rjcp libre -c $(COF)

include $(JAGSDK)/tools/build/jagrules.mk
