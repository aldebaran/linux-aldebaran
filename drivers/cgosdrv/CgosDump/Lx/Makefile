PROJECT_INC = -I. -I.. -I../.. -I../../CgosLib
PROJECT_LIB = -lcgos -L../../CgosLib/Lx
C_source = ../CgosDump.c
OPT = -Wall

default:
	gcc  $(C_source) -o cgosdump $(OPT) $(DEF) $(PROJECT_INC) $(PROJECT_LIB)

clean:
	rm -f cgosdump

cleanall: clean

