all: kernel test

kernel: kernelBTree.c
	gcc -o kernel kernelBTree.c

test: test.c
	gcc -o test test.c 

clean: 
	rm kernel
	rm test
