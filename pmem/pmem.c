#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "btree.h"

int main() {

		long long unsigned int pmem_addr = 0;
		pmem_addr = pmem_init();

		insertTest((void *)pmem_addr);
		searchTest();
		removeTest();
		searchTest();
		insertTest((void *)pmem_addr);
		searchTest();

		pmem_addr = pmem_load();
		
		searchTest();
		removeTest();
		searchTest();
		insertTest((void *)pmem_addr);
		searchTest();
		insertTest((void *)pmem_addr);
		searchTest();
		removeTest();
		searchTest();

		pmem_addr = pmem_exit();	
		return 0;
}
