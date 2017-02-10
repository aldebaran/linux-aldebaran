PROJECT_INC = -I. -I.. -I../.. -I../../CgosLib
PROJECT_LIB = -lcgos -L../../CgosLib/Lx
C_source = ../CgosMon.c
OPT = -Wall

default:
	gcc $(C_source) -o cgosmon $(OPT) $(DEF) $(PROJECT_INC) $(PROJECT_LIB)

clean:
	rm -f cgosmon

cleanall: clean

