#include <stdio.h>
#include <sys/syscall.h>
#include <linux/kernel.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define BTREE_INSERT 335
#define BTREE_SEARCH 336
#define BTREE_REMOVE 337
#define INK_INIT 339
#define INK_LOAD 338


int main() {
		printf("TEST START!\n");

		int ret = 19999;

		ret = syscall(INK_INIT);
		printf("ret: %d\n", ret);

		ret = 9999;
		
		ret = syscall(INK_LOAD);
		printf("ret: %d\n", ret);

		printf("TEST END!\n");
		return 0;
}
