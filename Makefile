# prodcons/Makefile
LIBS= -lpthread
PROGRAMS= prodcons0 prodcons1 prodcons2 prodcons3 spmc spmc2 ordering
CCOPTS= -Wall -pedantic -ansi -g   -ggdb  -fno-omit-frame-pointer 
#CCOPTS +=-fsanitize=address -static-libasan  -static-libstdc++   -fsanitize=thread
#arm-linux-gnueabihf-g++ -Wall -pedantic -ansi -g   -ggdb  -fno-omit-frame-pointer -lpthread spmc2.c -o spmc2_arm
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
spmc2: spmc2.c Makefile
	$(CC) $(CCOPTS) -o spmc2 spmc2.c $(LIBS)	
ordering: ordering.cpp
	gcc -o ordering -O2 ordering.cpp -lpthread	
clean:
	rm -f $(PROGRAMS) *.o *~ #*#