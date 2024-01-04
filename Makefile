# prodcons/Makefile
LIBS= -lpthread
PROGRAMS= prodcons0 prodcons1 prodcons2 prodcons3 spmc
CCOPTS= -Wall -pedantic -ansi
all: $(PROGRAMS)
prodcons0: prodcons0.c Makefile
	gcc $(CCOPTS) -o example prodcons0.c $(LIBS)
prodcons1: prodcons1.c Makefile
	gcc $(CCOPTS) -o prodcons1 prodcons1.c $(LIBS)
prodcons2: prodcons2.c Makefile
	gcc $(CCOPTS) -o prodcons2 prodcons2.c $(LIBS)
prodcons3: prodcons3.c Makefile
	gcc $(CCOPTS) -o prodcons3 prodcons3.c $(LIBS)
spmc: spmc.c Makefile
	g++ $(CCOPTS) -o spmc spmc.c $(LIBS)
clean:
	rm -f $(PROGRAMS) *.o *~ #*#