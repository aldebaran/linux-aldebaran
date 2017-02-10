
# Kernel-Makefile for building cgos for kernel version 2.6.x, 3.x and 4.x
#

# by default, the kernel source is assumed to be in
# /lib/modules/`uname -r`/build

# ATTENTION: use the install option with care: it`s assumed that the
# driver was build for the currently running kernel.

KERNELDIR ?= /lib/modules/`uname -r`/build
CONFIG_CGOS ?= m

C_source = DrvLx.c DrvOsaLx.c ../CgosDrv.c ../Cgeb.c

make26_pre16 = $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
make26 = $(MAKE) -C $(KERNELDIR) M=$(PWD)

sublevel = $(shell sed -n s/SUBLEVEL\ =\ *//p $(KERNELDIR)/Makefile)
patchlevel = $(shell sed -n s/PATCHLEVEL\ =\ *//p $(KERNELDIR)/Makefile)

mkcmd = $(shell if [ "$(version)" -lt 3 && "$(patchlevel)" == 6 && "$(sublevel)" -lt 16 ]; then echo $(make26_pre16); \
	else echo $(make26); fi; )

ifdef KERNELRELEASE

	EXTRA_CFLAGS += -I$(obj)/. -I$(obj)/.. -I$(obj)/../../CgosLib
	EXTRA_CFLAGS += -I$(obj)/../../CgosLib/Lx
#	EXTRA_CFLAGS += -O2 -mcmodel=kernel -DAMD64 -fno-strict-aliasing
#	EXTRA_CFLAGS += -O2 -DAMD64 -fno-strict-aliasing
	EXTRA_CFLAGS += -O2 -fno-strict-aliasing
	EXTRA_CFLAGS += -I$(obj)/../..
	obj-$(CONFIG_CGOS) += cgosdrv.o	

	cgosdrv-objs := DrvLx.o DrvOsaLx.o ../Cgeb.o ../CgosDrv.o 
	clean-files := *.o
else
	PWD := $(shell pwd)
	obj := $(PWD)

	DEF = -DUNIX -D__KERNEL__ -Dlinux -DMODULE
	EXTRA_CFLAGS = $(DEF) -O2 -Wall -Wl,-r -nostdlib
	EXTRA_CFLAGS += -I. -I.. -I../.. -I$(KERNELDIR)/include -I../../CgosLib
#	EXTRA_CFLAGS += -mcmodel=kernel -DAMD64 -fno-strict-aliasing
#	EXTRA_CFLAGS += -DAMD64 -fno-strict-aliasing
	EXTRA_CFLAGS += -fno-strict-aliasing

default:
	$(call mkcmd)

emu:
	gcc -o cgosdrv.o -D CGEBEMU $(EXTRA_CFLAGS) $(C_source) ../CgebEmu.c

endif

clean:
	rm -rf *~ *.ko *.mod.* .*.cmd .tmp* Drv*.o cgosdrv.o
	rm -rf DrvLx.o DrvOsaLx.o built-in.o
	rm -rf .*.flags
	rm -rf Module.symvers modules.order
	rm -rf ../*.o

install:
	install -m 644 -o root -g root cgosdrv.ko /lib/modules/`uname -r`/kernel/drivers/misc


