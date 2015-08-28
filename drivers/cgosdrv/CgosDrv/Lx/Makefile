# Kernel-Makefile for building cgos for kernel version 3.x, 2.6.x and 2.4.x
#

# by default, the kernel source is assumed to be in
# /lib/modules/`uname -r`/build

# if the sublevel for kernel 2.6 couldn't determined, the make command
# defaults to make26_1. If your build process doesn't compile correctly
# change it to make26_2 (echo command in line 28 of this file).

# ATTENTION: use the install option with care: it`s assumed that the
# driver was build for the currently running kernel.

KERNELDIR ?= /lib/modules/`uname -r`/build
CONFIG_CGOS ?= m

C_source = DrvLx.c DrvOsaLx.c ../CgosDrv.c ../Cgeb.c

make24	= gcc -o cgosdrv.o $(EXTRA_CFLAGS) $(C_source) 
make26_1 = $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
make26_2 = $(MAKE) -C $(KERNELDIR) M=$(PWD)
make30 = $(MAKE) -C $(KERNELDIR) M=$(PWD)

version = $(shell sed -n s/^VERSION\ =\ *//p $(KERNELDIR)/Makefile)
sublevel = $(shell sed -n s/^SUBLEVEL\ =\ *//p $(KERNELDIR)/Makefile)
patchlevel = $(shell sed -n s/^PATCHLEVEL\ =\ *//p $(KERNELDIR)/Makefile)

mkcmd = $(shell if [ "$(version)" = 3 ]; then echo $(make30); \
	else if [ "$(patchlevel)" = 4 ]; then echo $(make24); \
	else if [ -z "$(sublevel)" ]; then echo $(make26_1); \
	else if [ "$(sublevel)" -lt 16 ]; then echo $(make26_1); \
	else echo $(make26_2); fi; fi; fi; fi; )

# rules for 2.4.x versions of kernel
DO24 = yes
ifeq ($(version),3)
DO24 = no
endif

ifeq ($(patchlevel),6)
DO24 = no
endif

ifdef KERNELRELEASE
ifeq ($(DO24),yes)
# rules for kernel version 2.4.x

	EXTRA_CFLAGS += -I./.. -I./../../CgosLib
	O_TARGET := cgosdrv.o

	list-multi	:= cgosdrv.o
	cgos-objs	:=  ../Cgeb.o ../CgosDrv.o DrvLx.o DrvOsaLx.o

	obj-$(CONFIG_CGOS) += cgosdrv.o	

	include $(TOPDIR)/Rules.make

cgosdrv.o: $(cgos-objs)
	$(LD) -r -o $@ $(cgos-objs)

else
# rules for kernel versions 2.6.x and 3.x

	EXTRA_CFLAGS += -I$(obj)/. -I$(obj)/.. -I$(obj)/../.. -I$(obj)/../../CgosLib
	EXTRA_CFLAGS += -I$(obj)/../../CgosLib/Lx
	obj-$(CONFIG_CGOS) += cgosdrv.o	

	cgosdrv-objs := DrvLx.o DrvOsaLx.o ../Cgeb.o ../CgosDrv.o
	clean-files := *.o

endif

else
	PWD := $(shell pwd)
	obj := $(PWD)

	DEF = -DUNIX -D__KERNEL__ -Dlinux -DMODULE
	EXTRA_CFLAGS = $(DEF) -O2 -Wall -Wl,-r -nostdlib
	EXTRA_CFLAGS += -I. -I.. -I../.. -I$(KERNELDIR)/include -I../../CgosLib

default:
	$(call mkcmd)


emu:
	gcc -o cgosdrv.o -D CGEBEMU $(EXTRA_CFLAGS) $(C_source) ../CgebEmu.c

endif

clean:
	rm -rf *~ *.ko *.mod.* .*.cmd .tmp* *.o  
	rm -rf DrvLx.o DrvOsaLx.o built-in.o
	rm -rf .*.flags
	rm -rf Module.symvers
	rm -rf ../*.o
	rm -rf ../.*.cmd .tmp*
	rm -rf modules.order

install26:
	mkdir -p /lib/modules/`uname -r`/kernel/drivers/misc
	install -m 644 -o root -g root cgosdrv.ko /lib/modules/`uname -r`/kernel/drivers/misc
	depmod -a

install24:
	install -m 644 -o root -g root cgosdrv.o /lib/modules/`uname -r`/kernel
	depmod -a

install:
ifeq ($(DO24),yes)
	$(MAKE) install24
else
	$(MAKE) install26
endif 

