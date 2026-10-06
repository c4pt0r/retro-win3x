# Shared Win16 build rules (Open Watcom). In a project Makefile:
#   NAME = myapp
#   OBJS = myapp.obj other.obj      (optional, default $(NAME).obj)
#   GBK  = 1                        (optional: sources are UTF-8 with Chinese
#                                     text; converted to GBK before compiling)
#   include $(HOME)/win16dev/w16.mk
WATCOM  ?= $(HOME)/opt/watcom
export WATCOM
export PATH    := $(WATCOM)/binl64:$(WATCOM)/binl:$(PATH)
export INCLUDE := $(WATCOM)/h:$(WATCOM)/h/win
export EDPATH  := $(WATCOM)/eddat

OBJS    ?= $(NAME).obj
MODEL   ?= -ml
CFLAGS  ?= -bt=windows $(MODEL) -zW -w4 -ox -zq
LDOPTS  ?= option quiet option stack=8k option heapsize=4k
RES     := $(if $(wildcard $(NAME).rc),$(NAME).res)

all: $(NAME).exe

$(NAME).exe: $(OBJS) $(RES)
	wlink system windows name $@ $(foreach o,$(OBJS),file $(o)) $(LDOPTS)
ifneq ($(RES),)
	wrc -q -bt=windows $(RES) $@
endif

ifeq ($(GBK),1)
# compile a GBK copy in gbk/ (headers too, so #include "x.h" picks up GBK)
CFLAGS  += -zk1
gbk/%: %
	@mkdir -p gbk
	iconv -f utf-8 -t gbk $< > $@

%.obj: gbk/%.c $(patsubst %,gbk/%,$(wildcard *.h))
	wcc $(CFLAGS) -fo=$@ $<

%.obj: gbk/%.cpp $(patsubst %,gbk/%,$(wildcard *.h))
	wpp $(CFLAGS) -fo=$@ $<

%.res: gbk/%.rc $(patsubst %,gbk/%,$(wildcard *.h)) $(wildcard *.ico *.bmp *.cur)
	wrc -q -bt=windows -zk1 -r -i=. -fo=$@ $<
else
%.obj: %.c $(wildcard *.h)
	wcc $(CFLAGS) -fo=$@ $<

%.obj: %.cpp $(wildcard *.h)
	wpp $(CFLAGS) -fo=$@ $<

%.res: %.rc $(wildcard *.h *.ico *.bmp *.cur)
	wrc -q -bt=windows -r -fo=$@ $<
endif

# copy to the VM's A: drive
install: $(NAME).exe
	w31x put $(NAME).exe

# build, copy to VM, launch it there, save a screenshot
run: $(NAME).exe
	w16run $(NAME).exe

clean:
	rm -rf *.obj *.res *.err *.map $(NAME).exe gbk

.PHONY: all install run clean
.SECONDARY:
