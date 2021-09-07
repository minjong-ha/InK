#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>

#include "bp.h"

static pthread_rwlock_t rwlock;

void seqTest() {

		insertTest();
		searchTest();
		removeTest();
		searchTest();
		insertTest();
		searchTest();
		removeTest();
		searchTest();
		insertTest();

		bp_load();
		searchTest();
		removeTest();
		searchTest();
		insertTest();
		searchTest();
		removeTest();
		searchTest();


}

int main() {
		printf("test\n");

		bp_init();

		seqTest();
		//syncTest();


		//bp_exit();

		return 0;
}
