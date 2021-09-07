CFLAGS = $(shell pkg-config --cflags libpmem) -O2
LDFLAGS = $(shell pkg-config --libs libpmem) -O2
CC = gcc
RM = rm

%.o: %.cpp
	$(CC) -c $< -o $@ $(CFLAGS)

#all: main

#pmem: btree.c btree.h pmem.c info.c info.h
#gcc -o pmem pmem.c btree.h btree.c info.h info.c -lpthread
#	ctags -R

main: main.o bp.o
	gcc -o main main.o bp.o -lpthread -lpmem

main.o: main.c
	gcc -c -o main.o main.c 

bp.o: bp.c bp.h
	gcc -c -o bp.o bp.c
	ctags -R

clean:
	rm main
	rm *.o
