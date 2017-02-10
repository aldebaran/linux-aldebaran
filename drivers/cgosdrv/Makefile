ALLDIRS = CgosDrv CgosLib CgosDump CgosMon

# $Id: Makefile,v 1.5 2012-01-24 10:53:41 sml Exp $

# Determination of the target system
target = $(shell uname)

ifdef target
 ifeq ("$(target)","Linux")
  sysdir = Lx
  ext = lx
 endif

 ifeq ("$(target)","QNX")
  sysdir = Qx
  ext = qx
 endif
endif


# Definitions for packaging

# the package for Linux
ifeq ("$(sysdir)","Lx")
#  SHIP += CgosDrv/$(sysdir)/Config.in
#  SHIP += CgosDrv/$(sysdir)/Kconfig
#  SHIP += CgosDrv/$(sysdir)/DrvLx.c
#  SHIP += CgosDrv/$(sysdir)/DrvOsaLx.c
#  SHIP += CgosDrv/$(sysdir)/Makefile
#  SHIP += CgosLib/$(sysdir)/libcgosp.o
#  SHIP += CgosLib/$(sysdir)/LibOsaLx.c
#  SHIP += CgosLib/$(sysdir)/Makefile
#  SHIP += CgosDrv/DrvUla.h
#  SHIP += CgosDrv/DrvOsa.h
#  SHIP += CgosLib/LibOsa.h
#  SHIP += CgosLib/CgosIobd.h
#  SHIP += CgosLib/LibOsaM.c
endif

# the package for QNX
ifeq ("$(sysdir)","Qx")
#  SHIP =  CgosDrv/$(sysdir)/cgosdrv 
#  SHIP += CgosDump/$(sysdir)/cgosdump
#  SHIP += CgosMon/$(sysdir)/cgosmon
#  SHIP += CgosLib/$(sysdir)/libcgos.so
#  SHIP += CgosLib/$(sysdir)/libcgos.a
endif

# all generic components
#  SHIP += CgosLib/Cgos.h
#  SHIP += CgosDump/$(sysdir)/Makefile
#  SHIP += CgosDump/CgosDump.c
#  SHIP += CgosDump/CgosRprt.c
#  SHIP += CgosMon/$(sysdir)/Makefile
#  SHIP += CgosMon/OsAL.h
#  SHIP += CgosMon/CgosMon.c
#  SHIP += CgosDrv/$(sysdir)/readme

  BIN  += CgosLib/$(sysdir)/libcgos.so
  BIN  += CgosDump/$(sysdir)/cgosdump
  BIN  += CgosMon/$(sysdir)/cgosmon
	 

M = for i in $(ALLDIRS); do make -C $$i/$(sysdir)
N = || exit 1; done


major = $(shell awk '/define CgosLibVersionMajor/ {printf ("%d", $$3)}' CgosLib/Cgos.h)
minor = $(shell awk '/define CgosLibVersionMinor/ {printf ("%02d", $$3)}' CgosLib/Cgos.h)
build = $(shell awk '/define CGOS_BUILD_NUMBER/ {printf ("%03d", $$3)}' CgosBld.h)

default:
	$(M) $(N)

clean:
	$(M) $@ $(N)

cleanall:
	$(M) clean $(N)
	rm -rf ship cgos$(ext).tar.gz cgos$(ext)-$(major).$(minor).$(build)

emu:
	make -C CgosDrv/$(sysdir) $@

install:
	install -m 755 -o root -g root CgosDump/$(sysdir)/cgosdump /usr/bin
	install -m 755 -o root -g root CgosMon/$(sysdir)/cgosmon /usr/bin
	install -m 755 -o root -g root CgosLib/$(sysdir)/libcgos.so /usr/lib
	$(MAKE) -C CgosDrv/$(sysdir) install

mod:
	modprobe cgosdrv

recode:
ifeq ("$(sysdir)","Lx")
	recode -f ibmpc..lat1 CgosBld.h
	recode -f ibmpc..lat1 CgosLib/Cgos.h
endif


#mship:  $(mship_deps)
#	rm -rf ship cgos$(ext).tar.gz cgos$(ext)-$(major).$(minor).$(build)
#	$(M) $(N)
#	$(MAKE) mship2

#mship2:
#	mkdir -p ship/$(sysdir)/bin
#	for i in $(ALLDIRS); do mkdir -p ship/$(sysdir)/$$i/$(sysdir); done
#	for i in $(SHIP); do cp $$i ship/$(sysdir)/$$i; done
#	for i in $(BIN); do cp $$i ship/$(sysdir)/bin; done

#	mv ship/$(sysdir)/Makefile.user ship/$(sysdir)/Makefile
#	mv ship/$(sysdir)/CgosDrv/$(sysdir)/readme ship/$(sysdir)

#	ln -s ship/$(sysdir) cgos$(ext)-$(major).$(minor).$(build)
#	tar -czf cgos$(ext).tar.gz cgos$(ext)-$(major).$(minor).$(build)/*


