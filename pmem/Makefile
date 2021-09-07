all: pmem

#pmem: btree.c btree.h pmem.c info.c info.h
#gcc -o pmem pmem.c btree.h btree.c info.h info.c -lpthread
#	ctags -R

pmem: pmem.o btree.o info.o
	gcc -o pmem pmem.o btree.o info.o -lpthread
	ctags -R

pmem.o: pmem.c
	gcc -c -o pmem.o pmem.c 

btree.o: btree.c btree.h
	gcc -c -o btree.o btree.c

info.o: info.c info.h
	gcc -c -o info.o info.c 


clean:
	rm pmem
	rm *.o
