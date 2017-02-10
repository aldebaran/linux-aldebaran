ALLDIRS = CgosDrv CgosLib CgosDump CgosMon

# Determination of the target system
target = $(shell uname)
sysdir = Lx

ifdef target
 ifeq ("$(target)","QNX")
  sysdir = Qx
 endif
endif

M = for i in $(ALLDIRS); do make -C $$i/$(sysdir)
N = || exit 1; done

ifeq ("$(target)","QNX")
install:
	cp CgosDump/$(sysdir)/cgosdump /usr/bin
	cp CgosMon/$(sysdir)/cgosmon /usr/bin
	cp CgosLib/$(sysdir)/libcgos.so /usr/lib
	cp CgosDrv/$(sysdir)/cgosdrv /usr/lib
else

default:
	$(M) $(N)

install:
	install -m 755 -o root -g root CgosDump/$(sysdir)/cgosdump /usr/bin
	install -m 755 -o root -g root CgosMon/$(sysdir)/cgosmon /usr/bin
	install -m 755 -o root -g root CgosLib/$(sysdir)/libcgos.so /usr/lib
	install -m 644 -o root -g root CgosDrv/$(sysdir)/cgosdrv.ko /lib/modules/`uname -r`/kernel/drivers/misc 
	depmod -a

clean:
	$(M) $@ $(N)

endif



