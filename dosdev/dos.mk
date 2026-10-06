# Shared DOS build rules (Open Watcom). In a project Makefile:
#   NAME   = myprog
#   TARGET = dos16 | dos32        (dos32 = 32-bit protected mode, DOS/4GW)
#   OBJS   = myprog.obj ...       (optional, default $(NAME).obj)
#   QUITKEYS = esc y              (optional, keys `make run` sends to quit)
#   include $(HOME)/dosdev/dos.mk
WATCOM  ?= $(HOME)/opt/watcom
export WATCOM
export PATH    := $(WATCOM)/binl64:$(WATCOM)/binl:$(PATH)
export INCLUDE := $(WATCOM)/h
export EDPATH  := $(WATCOM)/eddat

TARGET  ?= dos16
OBJS    ?= $(NAME).obj

ifeq ($(TARGET),dos32)
CC      = wcc386
CFLAGS  ?= -bt=dos -mf -5r -w4 -ox -zq
SYSTEM  = dos4g
EXTRA   = DOS4GW.EXE
else
CC      = wcc
CFLAGS  ?= -bt=dos -ms -0 -w4 -ox -zq
SYSTEM  = dos
EXTRA   =
endif

all: $(NAME).exe $(EXTRA)

$(NAME).exe: $(OBJS)
	wlink system $(SYSTEM) name $@ $(foreach o,$(OBJS),file $(o)) option quiet

%.obj: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -fo=$@ $<

DOS4GW.EXE:
	cp $(WATCOM)/binw/dos4gw.exe $@

# copy to the VM's A: drive
install: all
	w31x put $(NAME).exe $(EXTRA)

# copy to VM, run it (full screen DOS session), screenshot, then send Esc
run: all
	dosrun $(if $(QUITKEYS),-q "$(QUITKEYS)") $(NAME).exe

clean:
	rm -f *.obj *.err *.map $(NAME).exe $(EXTRA)

.PHONY: all install run clean
