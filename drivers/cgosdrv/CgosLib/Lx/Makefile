PROJECT_INC = -I. -I.. -I../.. -I../../CgosLib
PROJECT_LIB =
C_source = LibOsaLx.c ../LibOsaM.c
C_source_p = ../Cgos.c
DEF = -DUNIX
OPT = -Wall -fPIC

default: $(C_source) libcgosp.o
	gcc -shared -o libcgos.so $(OPT) $(DEF) $(PROJECT_INC) $(C_source) libcgosp.o

libcgosp.o:
	gcc -Wl,-r -nostdlib -o libcgosp.o $(OPT) $(DEF) $(PROJECT_INC) $(C_source_p)

clean:
	rm -f *.so

cleanall:
	rm -f *.so *.o
