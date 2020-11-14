ALIGN=q
include $(JAGSDK)/tools/build/jagdefs.mk

CFLAGS += -Wall -fno-builtin

OBJS =	startup.o \
	testdrv.o \
	usb.o \
	sprintf.o \
	skunkc.o \
	util.o

COF = testdrv.cof

include $(JAGSDK)/jaguar/skunk/skunk.mk

PROGS = $(COF)

$(COF): $(OBJS)
	$(LINK) $(LINKFLAGS) -o $@ $(OBJS)

skunkc.o: $(SKUNKDIR)/lib/skunkc.s
	$(ASM) $(ASMFLAGS) -o $@ $<

.PHONY: run
run:	$(COF)
	rjcp libre -c $(COF)

include $(JAGSDK)/tools/build/jagrules.mk
