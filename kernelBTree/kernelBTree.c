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

int nr_test = 1000;
int random_cover = 999999;

int testLimit = 100000;

void randomInsert(char *key, char *value) {
		size_t key_len = strlen(key)+1;
		size_t value_len = strlen(value)+1;


		int ret = syscall(BTREE_INSERT, key, key_len, value, value_len);
		printf("insert: %s %s\n", key, value);
}

void randomSearch(char *key) {
		size_t key_len = strlen(key)+1;

		char *ret_value = malloc(sizeof(char)*4096);


		int ret = syscall(BTREE_SEARCH, key, key_len, ret_value);
		printf("search: %s\n", key);
}

void randomRemove(char *key) {
		size_t key_len = strlen(key)+1;


		int ret = syscall(BTREE_REMOVE, key, key_len);
		printf("remove: %s\n", key);
}

void process_test() {
		int count = 0;
		srand(time(NULL));

		//     while(count < nr_test) {
		while(1) {
				int case_num = (rand() % 3);
				int kv_num = (rand() % random_cover);

				char *key = malloc(sizeof(char)*4096);
				char *value = malloc(sizeof(char)*4096);
				sprintf(key, "key_%d", kv_num);
				sprintf(value, "value_%d", kv_num);

				switch(case_num) {
						case 0:
								randomInsert(key, value);
								break;
						case 1:
								randomSearch(key);
								break;
						case 2:
								randomRemove(key);
								break;
				}

				free(key); free(value);
				count++;
		}
}

void removeTest() {
		int i = 0; int count = 0;

		int array[testLimit];
		for(int x = 0; x < testLimit; x++) {
				array[x] = x;
		}

		srand(time(NULL));
		for(int x = 0; x < testLimit * 2; x++) {
				int rand1 = rand() % testLimit;
				int rand2 = rand() % testLimit;
				int temp;

				temp = array[rand1];
				array[rand1] = array[rand2];
				array[rand2] = temp;
		}


		for(i = 0; i < testLimit; i++) {
				char *key = malloc(sizeof(char)*4096);
				sprintf(key, "key_%d", array[i]);


				int ret = syscall(BTREE_REMOVE, key, strlen(key)+1);

				if(ret >= 0) {
						count++;
				}
				else {
						count = count;
				}

				free(key);
		}
		printf("removeTest count: %d\n", count);
}

//for test
void searchTest() {
		int i = 0;
		int count = 0;

		int array[testLimit];
		for(int x = 0; x < testLimit; x++) {
				array[x] = x;
		}

		srand(time(NULL));
		for(int x = 0; x < testLimit * 2; x++) {
				int rand1 = rand() % testLimit;
				int rand2 = rand() % testLimit;
				int temp;

				temp = array[rand1];
				array[rand1] = array[rand2];
				array[rand2] = temp;
		}
		for(i = 0 ; i < testLimit ; i++) {
				char *key = malloc(sizeof(char) * 40);
				char *ret_value = malloc(sizeof(char)*4096);

				sprintf(key, "key_%d", array[i]);

				size_t key_len = strlen(key);

				int ret = syscall(BTREE_SEARCH, key, strlen(key)+1, ret_value);
				if(ret == 0) {
						count++;
				}
				free(key);
				free(ret_value);
		}
		printf("searchTest count: %d\n", count);
}



void insertTest() {
		int i = 0;
		int count = 0;

		int array[testLimit];
		for(int x = 0; x < testLimit; x++) {
				array[x] = x;
		}

		srand(time(NULL));
		for(int x = 0; x < testLimit * 2; x++) {
				int rand1 = rand() % testLimit;
				int rand2 = rand() % testLimit;
				int temp;

				temp = array[rand1];
				array[rand1] = array[rand2];
				array[rand2] = temp;
		}

		for(i = 0 ; i < testLimit;  i++) {
				char *key = malloc(sizeof(char)*40);
				char *value = malloc(sizeof(char)*40);

				sprintf(key, "key_%d", array[i]);
				sprintf(value, "value_%d", array[i]);

				int ret = syscall(BTREE_INSERT, key, strlen(key)+1, value, strlen(value)+1);

				if(ret == 0)
						count++;

		}
		printf("insert count: %d\n", count);

}

int main() {
//		int pidLimit = 10;
//		pid_t pids[pidLimit];
//		int runProcess = 0;
//		int state;
//
//		while(runProcess < pidLimit) {
//				pids[runProcess] = fork();
//
//				if(pids[runProcess] == 1) {
//						process_test();
//				}
//				else {
//						process_test();
//				}
//				runProcess++;
//		}

		insertTest();
		searchTest();
		removeTest();
		searchTest();
		insertTest();
		searchTest();

		return 0;
}

