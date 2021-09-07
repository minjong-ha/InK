#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>

#include <libpmem.h>
#include <errno.h>

#include <stddef.h>

#include "bp.h"

#define PMEM_PATH "/mnt/mypmem/pmem_test_file"
//#define PMEM_CAP 1024 * 1024 * 1024 * 4
//#define JOUR_CAP 1024 * 1024 * 1024 * 1

static pthread_rwlock_t list_rwLock;

int testLimit = 10000;

int nr_test = 10000;
int random_cover = 99999;

void *pmem;
void **meta;

struct bp_node *root;
void *next_ptr;
struct free_list *data_arr;

void *jour_addr;
void *jour_curr;;

struct bp_node *bp_node_create() {
		struct bp_node *new_node = alloc_bp_node();
		memset(new_node, 0, sizeof(struct bp_node));

		new_node->is_leaf = FALSE;

		new_node = (struct bp_node *)((void *)new_node - pmem);

		return new_node;
}
void bp_init() {
		printf("\n=====PMEM_INIT=====\n");

		//pmem = malloc(sizeof(char) * (PMEM_CAP + JOUR_CAP));
		//printf("%lld %lld\n", PMEM_CAP, JOUR_CAP);
		//=================================================
		size_t mapped_len;
		int is_pmem;

		if((pmem=(void *)pmem_map_file(PMEM_PATH, PMEM_CAP + JOUR_CAP, PMEM_FILE_CREATE, 0666, &mapped_len, &is_pmem)) == NULL) {
				perror("pmem_map_file");
				exit(1);
		}
		//=================================================

		meta = (void **)pmem;
		printf("pmem: %llx\n", pmem);

		jour_addr = (void *)PMEM_CAP + (size_t)pmem + 16;
		jour_curr = jour_addr;


		printf("jour_addr: %llx\n", jour_addr);

		int i = 0;
		/*
		   meta[0] : root_node
		   meta[1] : next_ptr
		   meta[2] : jour_addr offset
		   meta[3] : jour_curr offset
		   meta[4] : data_arr[]
		 */
		meta[1] = (void *) (1024L * 1024L * 1024L * 2L);
		meta[2] = (void *)jour_addr - (size_t)pmem;
		meta[3] = (void *)jour_curr - (size_t)pmem;

		next_ptr = meta[1] + (size_t)pmem;

		data_arr = (struct free_list *)&meta[4];
		for(i = 0; i < 512; i++) {
				data_arr[i].head = NULL;
				data_arr[i].tail = NULL;
				data_arr[i].nr_node = 0;
		}

		struct bp_node *root_offset = bp_node_create();
		meta[0] = root_offset;
		root = (void *)root_offset + (size_t)pmem;
		root->is_leaf = TRUE;

		printf("meta[2]: %llx meta[3]: %llx\n", meta[2], meta[3]);
		printf("root: %llx next_ptr: %llx jour_addr: %llx jour_curr: %llx data_arr: %llx\n", root, next_ptr, jour_addr, jour_curr, data_arr);
}

void bp_load() {
		printf("\n=====PMEM_LOAD=====\n");
		printf("meta[3]: %llx\n", meta[3]);

		int i = 0;

		meta           = NULL;
		root           = NULL;
		next_ptr       = NULL;
		data_arr       = NULL;
		jour_addr = NULL;
		jour_curr = NULL;
		data_arr = NULL;

		meta           = (void **)pmem;
		root         	 = meta[0] + (size_t)pmem;
		next_ptr 	 = meta[1] + (size_t)pmem;
		jour_addr      = meta[2] + (size_t)pmem;
		jour_curr      = meta[3] + (size_t)pmem;
		data_arr       = (struct free_list *)&meta[4];

		printf("meta[2]: %llx meta[3]: %llx\n", meta[2], meta[3]);
		printf("root: %llx next_ptr: %llx jour_addr: %llx jour_curr: %llx data_arr: %llx\n", root, next_ptr, jour_addr, jour_curr, data_arr);

}

void bp_exit() {
		free(pmem);
}

void backup_process(void *target_offset, size_t target_size) {
		size_t first_leng;
		size_t second_leng;
		void *target_addr = (void *)target_offset + (size_t)pmem;

		if(target_offset == NULL){
				//do nothing
		}
		else {
				sprintf(jour_curr, "0x%012llx;%08ld;", target_offset, target_size);
				first_leng = strlen(jour_curr);
				jour_curr = jour_curr + first_leng;

				second_leng = target_size;
				memcpy(jour_curr, target_addr, second_leng);
				jour_curr = jour_curr + second_leng;

				strcpy(jour_curr, ";");
				jour_curr = jour_curr + 1;
		}
}

void recovery_process() {
		if(jour_addr != jour_curr) {
				void *backup_data = malloc(sizeof(char) * (jour_curr - jour_addr));
				void *backup_curr = backup_data;
				memcpy(backup_data, jour_addr, sizeof(char) * (jour_curr - jour_addr));

				//copy backup data in NVMe to main memory
				size_t *offset = malloc(sizeof(void *) * 1024);
				size_t *length = malloc(sizeof(void *) * 1024);
				void **target = malloc(sizeof(void *) * 1024 * 1024);
				int i = 0;

				//Ordering
				while((backup_curr-backup_data) <= (jour_curr-jour_addr)) {

						char *offset_data = malloc(sizeof(char) * 14);
						strncpy(offset_data, backup_curr, sizeof(char) * 14);
						backup_curr += 15;
						offset[i] = strtol(offset_data, NULL, 16);

						char *length_data = malloc(sizeof(char) * 8);
						strncpy(length_data, backup_curr, sizeof(char)*8);
						backup_curr += 9;
						length[i] = strtol(length_data, NULL, 10);

						//memcpy(target, backup_curr, sizeof(char) * length);
						target[i] = backup_curr;
						backup_curr = (void *)backup_curr + (size_t)(length[i] + 1);

						free(offset_data);
						free(length_data);
						i++;
				}

				i = i - 2;
				for(int x = i; x >= 0; x--) {
						void *target_addr = (void *)offset[x] + (size_t)pmem;
						printf("target_offset: %p target_addr: %p, length[%d]: %d, target[%d]: %p\n", offset[x], target_addr, x, length[x], x, target[x]);
						//memcpy need
						memcpy(target_addr, target, length[x]);
				}


				free(backup_data);
				free(offset);
				free(length);
				free(target);
		}
		else {
				//printf("NO NEED TO RECOVERY. EVERYTHING FINE\n");
		}
}

void init_recov() {
		memset(jour_addr, 0, jour_curr - jour_addr);
		jour_curr = jour_addr;
}

void clear_bp_node(struct bp_node *input_node) {
		struct bp_node *node = (void *)input_node + (size_t)pmem;

		//do not touch is_lock
		//memset(node, 0, offsetof(struct bp_node, is_lock));
		memset(node, 0, offsetof(struct bp_node, rwLock) - 1);
}

void free_bp_node(struct bp_node *input_node) {
		if(input_node == NULL) {
				//do nothing
		}
		else {
				int index = sizeof(struct bp_node) / 16;

				//==================================================
				pthread_rwlock_wrlock(&list_rwLock);
				//==================================================

				clear_bp_node(input_node);

				//backup need
				void *list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
				backup_process(data_arr[index].head, sizeof(struct free_node));
				backup_process(data_arr[index].tail, sizeof(struct free_node));
				backup_process(list_offset, sizeof(struct free_list));

				struct free_node *fn = (void *)input_node;
				struct free_node *tmp_fn = (void *)fn + (size_t)pmem;
				tmp_fn->next = NULL;

				if(data_arr[index].nr_node == 0) {
						data_arr[index].head = (void *)fn;
						data_arr[index].tail = (void *)fn;
						data_arr[index].nr_node++;
				}
				else if(data_arr[index].nr_node > 0) {
						struct free_node *tail_fn = (void *)data_arr[index].tail + (size_t)pmem;
						tail_fn->next = (void *)fn;
						data_arr[index].tail = (void *)fn;
						data_arr[index].nr_node++;
				}
		}

		//==================================================
		pthread_rwlock_unlock(&list_rwLock);
		//==================================================
}

struct bp_node *alloc_bp_node() {
		struct bp_node *ret = NULL;

		int index = sizeof(struct bp_node) / 16;


		//==================================================
		//GET_LOCK
		pthread_rwlock_wrlock(&list_rwLock);
		//==================================================

		//backup need
		void *list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
		backup_process(data_arr[index].head, sizeof(struct free_node));
		backup_process(data_arr[index].tail, sizeof(struct free_node));
		backup_process(list_offset, sizeof(struct free_list));


		if(data_arr[index].nr_node > 0) {
				struct free_node *fn = (void *)data_arr[index].head + (size_t)pmem;
				data_arr[index].head = fn->next;
				ret = (void *)fn;
				data_arr[index].nr_node--;
		}
		else if(data_arr[index].nr_node == 0) {
				ret = next_ptr;
				meta[1] = meta[1] + ((index + 1) * 16);
				next_ptr = next_ptr + ((index + 1) * 16);
		}

		//==================================================
		//PUT_LOCK
		pthread_rwlock_unlock(&list_rwLock);
		//==================================================
		return ret;
}

void free_string(char *input_string) {
		if(input_string == NULL) {
				//do nothing
		}
		else {
				char *string = (void *)input_string + (size_t)pmem;
				int index = strlen(string)/16;
				string = NULL;

				//==================================================
				//GET_LOCK
				pthread_rwlock_wrlock(&list_rwLock);
				//==================================================

				//backup need
				void *list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
				backup_process(data_arr[index].head, sizeof(struct free_node));
				backup_process(data_arr[index].tail, sizeof(struct free_node));
				backup_process(list_offset, sizeof(struct free_list));


				struct free_node *fn = (void *)input_string;
				struct free_node *tmp_fn = (void *)fn + (size_t)pmem;
				tmp_fn->next = NULL;

				if(data_arr[index].nr_node == 0) {
						data_arr[index].head = (void *)fn;
						data_arr[index].tail = (void *)fn;
						data_arr[index].nr_node++;
				}
				else if(data_arr[index].nr_node > 0) {
						struct free_node *tail_fn = (void *)data_arr[index].tail + (size_t)pmem;
						tail_fn -> next = (void *)fn;
						data_arr[index].tail = (void *)fn;
						data_arr[index].nr_node++;
				}

		}

		//==================================================
		//PUT_LOCK
		pthread_rwlock_unlock(&list_rwLock);
		//==================================================
}

char *alloc_string(size_t length) {
		char *ret = NULL;
		int index = length /16;

		//==================================================
		//GET_LOCK
		pthread_rwlock_wrlock(&list_rwLock);
		//==================================================

		//backup need
		void *list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
		backup_process(data_arr[index].head, sizeof(struct free_node));
		backup_process(data_arr[index].tail, sizeof(struct free_node));
		backup_process(list_offset, sizeof(struct free_list));


		if(data_arr[index].nr_node > 0) {
				struct free_node *fn = (void *)data_arr[index].head + (size_t)pmem;
				data_arr[index].head = fn->next;
				ret = (void *)fn;
				data_arr[index].nr_node--;
		}
		else if(data_arr[index].nr_node == 0) {
				ret = next_ptr;
				meta[1] = meta[1] + ((index + 1) * 16);
				next_ptr = next_ptr + ((index + 1) * 16);
		}

		//==================================================
		//PUT_LOCK
		pthread_rwlock_unlock(&list_rwLock);
		//==================================================

		return ret;

}

int findValue(struct bp_node *input_node, char *input_key) {
		struct bp_node *node = (void *)input_node + (size_t)pmem;
		char *key = (void *)input_key + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		while(low <= high) {
				int mid = (low + high) / 2;

				//problem happen!
				//printf("node->index: %llx\n", node->index[mid]);
				char *mid_key = (void *)node->index[mid] + (size_t)pmem;

				if(strcmp(key, mid_key) == 0) {
						return mid;
				}
				else {
						if(strcmp(key, mid_key) > 0)
								low = mid + 1;
						else
								high = mid - 1;
				}
		}

		return -1;
}

void removeValue(struct bp_node *input_node, int index) {
		struct bp_node *node = (void *)input_node + (size_t)pmem; 

		//backup need
		char *backup_key, *backup_value;
		backup_key = (void *)node->index[index] + (size_t)pmem;
		backup_value = (void *)node->children[index] + (size_t)pmem;
		backup_process(node->index[index], strlen(backup_key));
		backup_process(node->children[index], strlen(backup_value));
		backup_process(input_node, sizeof(struct bp_node));

		//free(node->index[index]);
		free_string(node->index[index]);
		node->index[index] = NULL;
		//free(node->children[index]);
		free_string(node->children[index]);
		node->children[index] = NULL;

		if(index <= node->nr_chunks - 1) {
				changeOffset(&node->index[index], &node->index[index+1], sizeof(void *) * (node->nr_chunks - index - 1));
				changeOffset(&node->children[index], &node->children[index+1], sizeof(struct BTreeNode *) * (node->nr_chunks - index));
		}
		node->nr_chunks--;
}

struct bp_node *bp_node_splitRoot(struct bp_node *input_parentNode) {
		struct bp_node *parentNode = (void *)input_parentNode + (size_t)pmem;

		//parentNode == root 

		struct bp_node *left, *right;
		struct bp_node *split;

		split = parentNode;

		backup_process(input_parentNode, sizeof(struct bp_node));

		left = bp_node_create();
		left = (void *)left + (size_t)pmem;

		right = bp_node_create();
		right = (void *)right + (size_t)pmem;

		if(root->is_leaf == TRUE) {
				//do leaf operation
				changeOffset(&left->index[0], &split->index[0], sizeof(void *) * ((INDEX_MAX/2) + 1));
				changeOffset(&left->children[0], &split->children[0], sizeof(void *) * ((INDEX_MAX/2) + 2));
				left->nr_chunks = (INDEX_MAX / 2) + 1;
				left->is_leaf = TRUE;

				changeOffset(&right->index[0], &split->index[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2) + 1)));
				changeOffset(&right->children[0], &split->children[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1) + 1));
				right->nr_chunks = INDEX_MAX / 2;
				right->is_leaf = TRUE;

				split->is_leaf = FALSE;
		}
		else {
				//do non-leaf(internal node) operation
				changeOffset(&left->index[0], &split->index[0], sizeof(void *) * (INDEX_MAX/2));
				changeOffset(&left->children[0], &split->children[0], sizeof(void *) * ((INDEX_MAX/2) + 1));
				left->nr_chunks = INDEX_MAX / 2;
				left->is_leaf = FALSE;

				changeOffset(&right->index[0], &split->index[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2) + 1)));
				changeOffset(&right->children[0], &split->children[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1) + 1));
				right->nr_chunks = INDEX_MAX / 2;
				right->is_leaf = FALSE;

				split->is_leaf = FALSE;
		}

		//char *temp = malloc(sizeof(char) * strlen(split->index[INDEX_MAX/2]));
		//strcpy(temp, split->index[INDEX_MAX/2]);
		char *cp = (void *)split->index[INDEX_MAX/2] + (size_t)pmem;
		char *temp = alloc_string(strlen(cp));
		strcpy(temp, cp);

		left = (void *)((void *)left - pmem);
		right = (void *)((void *)right - pmem);
		split = (void *)((void *)split - pmem);
		temp = (void *)((void *)temp - pmem);
		clear_bp_node(split);
		addValue(split, temp, left, right);

		return input_parentNode;
}

struct bp_node *bp_node_splitNormal(struct bp_node *input_currentNode, struct bp_node *input_parentNode, char *input_key) {
		struct bp_node *currentNode = (void *)input_currentNode + (size_t)pmem;
		struct bp_node *parentNode = (void *)input_parentNode + (size_t)pmem;
		char *key = (void *)input_key + (size_t)pmem;

		struct bp_node *left, *right;
		struct bp_node *split;

		split = parentNode;

		int i = nextNodeIndex(input_parentNode, input_key);

		//backup need
		backup_process(input_parentNode, sizeof(struct bp_node));

		if(currentNode->is_leaf == TRUE) {
				//backup_need
				char *backup_value = (void *)parentNode->children[i] + (size_t)pmem;
				backup_process(parentNode->children[i], sizeof(strlen(backup_value)));

				//do leaf operation
				left = parentNode->children[i];
				left = (void *)left + (size_t)pmem;
				left->nr_chunks = (INDEX_MAX / 2) + 1;
				left->is_leaf = TRUE;

				right = bp_node_create();
				right = (void *)right + (size_t)pmem;
				right->nr_chunks = INDEX_MAX / 2;
				right->is_leaf = TRUE;

				split->is_leaf = FALSE;

				changeOffset(&right->index[0], &left->index[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1)));
				changeOffset(&right->children[0], &left->children[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1) + 1));
		}

		else {
				//backup need
				backup_process(parentNode->children[i], sizeof(struct bp_node));

				//do non-leaf(internal node) operation
				left = parentNode->children[i];
				left = (void *)left + (size_t)pmem;
				left->nr_chunks = INDEX_MAX / 2;
				left->is_leaf = FALSE;

				right = bp_node_create();
				right = (void *)right + (size_t)pmem;
				right->nr_chunks = INDEX_MAX / 2;
				right->is_leaf = FALSE;

				split->is_leaf = FALSE;

				changeOffset(&right->index[0], &left->index[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1)));
				changeOffset(&right->children[0], &left->children[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1) + 1));
		}

		char *cp = (void *)left->index[INDEX_MAX/2] + (size_t)pmem;
		char *temp = malloc(strlen(cp));
		strcpy(temp, cp);

		left = (void *)((void *)left - pmem);
		right = (void *)((void *)right - pmem);
		temp = (void *)((void *)temp - pmem);
		parentNode = (struct bp_node *)((void *)parentNode - pmem);
		addValue(parentNode, temp, left, right);

		struct bp_node *offset_split = (void *)((void *)split - pmem);

		//==================================================
		//PUT_PATH
	//	pthread_rwlock_unlock(&currentNode->rwLock);
		//==================================================

		return offset_split;
}

void changeOffset(void *dest, void *src, size_t size) {

		if(size !=0) {
				void *mem_node = malloc(sizeof(void *) * (size));
				memset(mem_node, 0, size);

				memcpy(mem_node, src, size);
				memset(dest, 0, size);

				memcpy(dest, mem_node, size);

				free(mem_node);
		}
}

int nextNodeIndex(struct bp_node *input_node, char *input_key) {
		struct bp_node *node = (void *)input_node + (size_t)pmem;
		char *key = (void *)input_key + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		while(low <=high) {
				int mid = (low + high) / 2;

				char *mid_key = (void *)node->index[mid] + (size_t)pmem;

				if(strcmp(key, mid_key) > 0)
						low = mid + 1;
				else
						high = mid - 1;
		}

		if( low == high)
				return low + 1;
		else
				return low;

}

int nextNodeIndex_search(struct bp_node *input_node, char *key) {
		struct bp_node *node = (void *)input_node + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		while(low <=high) {
				int mid = (low + high) / 2;

				char *mid_key = (void *)node->index[mid] + (size_t)pmem;

				if(strcmp(key, mid_key) > 0)
						low = mid + 1;
				else
						high = mid - 1;
		}

		if( low == high)
				return low + 1;
		else
				return low;
}


void addValue(struct bp_node *input_node, char *input_key, void *input_left, void *input_right) {
		struct bp_node *node = (void *)input_node + (size_t)pmem;
		char *key = (void *)input_key + (size_t)pmem;
		void *left = (void *)input_left + (size_t)pmem;
		void *right = (void *)input_right + (size_t)pmem;

		int i = nextNodeIndex(input_node, input_key);

		//recovery_need
		backup_process(input_node, sizeof(struct bp_node));

		node->index[node->nr_chunks] = NULL;
		node->children[node->nr_chunks + 1] = NULL;

		if(node->is_leaf == TRUE) {

				changeOffset(&node->index[i+1], &node->index[i], sizeof(void *) * (node->nr_chunks - i));
				changeOffset(&node->children[i+1], &node->children[i], sizeof(void *) * (node->nr_chunks - i));

				node->nr_chunks++;
				node->index[i] = input_key;
				node->children[i] = input_left;
				//node->children[i+1] = right;
		}

		else {
				//do non-leaf operation
				changeOffset(&node->index[i+1], &node->index[i], (sizeof(void *) * (node->nr_chunks - i)));
				changeOffset(&node->children[i+2], &node->children[i+1], sizeof(void *) * (node->nr_chunks - i));

				node->nr_chunks++;
				node->index[i] = input_key;
				node->children[i] = input_left;
				node->children[i+1] = input_right;
		}

		//===
}

int bp_insert(char *input_key, char *input_value) {
		char *key = (void *)input_key + (size_t)pmem;
		char *value = (void *)input_value + (size_t)pmem;

		struct bp_node *currentNode, *parentNode;

		parentNode = root;
		currentNode = parentNode;
		currentNode = (struct bp_node *)((void *)currentNode - pmem);

		//condition might need change
		while(TRUE) {
				currentNode = (void *)currentNode + (size_t)pmem;
				struct bp_node *input_currentNode = (struct bp_node *)((void *)currentNode - pmem);
				struct bp_node *input_parentNode = (struct bp_node *)((void *)parentNode - pmem);
				struct bp_node *origin_splitNormal;
				int is_splitNormal = FALSE;

				//==================================================
				//GET LOCK
				if(currentNode == root) {
						pthread_rwlock_wrlock(&currentNode->rwLock);
				}
				else {
						pthread_rwlock_wrlock(&parentNode->rwLock);
						pthread_rwlock_wrlock(&currentNode->rwLock);
				}
				//==================================================

				int ret_findValue = findValue(input_currentNode, input_key);

				if(ret_findValue >= 0 && currentNode->is_leaf == TRUE) {
						//update!
						char *legacy_value = (void *)currentNode->children[ret_findValue] + (size_t)pmem;
						backup_process(currentNode->children[ret_findValue], strlen(legacy_value));
						free_string(currentNode->children[ret_findValue]);
						currentNode->children[ret_findValue] =  input_value;

						free_string(input_key);

						//==================================================
						//PUT LOCK
						if(currentNode == root) {
								pthread_rwlock_unlock(&currentNode->rwLock);
						}
						else {
								pthread_rwlock_unlock(&currentNode->rwLock);
								pthread_rwlock_unlock(&parentNode->rwLock);
						}
						//==================================================
						return 1;
				}

				if(currentNode->nr_chunks == INDEX_MAX) {
						if(currentNode == root) {
								currentNode = bp_node_splitRoot(input_currentNode);
								currentNode = (void *)currentNode + (size_t)pmem;
						}
						else {
								origin_splitNormal = currentNode;
								is_splitNormal = TRUE;
								currentNode = bp_node_splitNormal(input_currentNode, input_parentNode, input_key);
								currentNode = (void *)currentNode + (size_t)pmem;
						}
				}

				if(currentNode->is_leaf == TRUE) {
						break;
				}

				//parentNode = currentNode;
				//input_parentNode = (struct bp_node *)((void *)parentNode - pmem);
				input_currentNode = (struct bp_node *)((void *)currentNode - pmem);

				int next = nextNodeIndex(input_currentNode, input_key);

				//==================================================
				//PUT_PATH
				if(currentNode == root && is_splitNormal == FALSE) {
						pthread_rwlock_unlock(&currentNode->rwLock);
				}
				else if (is_splitNormal == TRUE) {
						is_splitNormal = FALSE;
						pthread_rwlock_unlock(&origin_splitNormal->rwLock);
						pthread_rwlock_unlock(&currentNode->rwLock);
				}
				else {
						pthread_rwlock_unlock(&currentNode->rwLock);
						pthread_rwlock_unlock(&parentNode->rwLock);
				}
				//==================================================

				parentNode = currentNode;
				currentNode = currentNode->children[next];
		}

		struct bp_node *input_currentNode = (struct bp_node *)((void *)currentNode - pmem);

		addValue(input_currentNode, input_key, input_value, NULL);

		//==================================================
		//PUT_PATH
		if(currentNode == root) {
				pthread_rwlock_unlock(&currentNode->rwLock);
		}
		else {
				pthread_rwlock_unlock(&currentNode->rwLock);
				pthread_rwlock_unlock(&parentNode->rwLock);
		}
		//==================================================

		return 0;
}

int bp_search(char *key) {
		struct bp_node *currentNode;
		int index = 0;

		//pthread_rwlock_rdlock(&rwLock);

		currentNode = (struct bp_node *)((void *)root - pmem);

		while(TRUE) {
				currentNode = (void *)currentNode + (size_t)pmem;
				struct bp_node *input_currentNode = (struct bp_node *)((void *)currentNode - pmem);
				//==================================================
				//GET_PATH
				pthread_rwlock_rdlock(&currentNode->rwLock);
				//==================================================

				index = nextNodeIndex_search(input_currentNode, key);

				if(currentNode->is_leaf == TRUE) {
						break;
				}

				//==================================================
				//PUT_PATH
				pthread_rwlock_unlock(&currentNode->rwLock);
				//==================================================
				currentNode = currentNode->children[index];

		}

		struct bp_node *input_currentNode = (struct bp_node *)((void *)currentNode - pmem);

		char *find_key = (void *)currentNode->index[index] + (size_t)pmem;
		char *find_value = (void *)currentNode->children[index] + (size_t)pmem;

		if(strcmp(find_key, key) != 0) {
				//==================================================
				//PUT_PATH
				pthread_rwlock_unlock(&currentNode->rwLock);
				//==================================================
				return -1;
		}

		else {
				//==================================================
				//PUT_PATH
				pthread_rwlock_unlock(&currentNode->rwLock);
				//==================================================

				return 0;
		}
}

int bp_remove(char *key) {
		struct bp_node *currentNode;
		int index = 0;

		currentNode = (struct bp_node *)((void *)root - pmem);

		while(TRUE) {
				currentNode = (void *)currentNode + (size_t)pmem;
				struct bp_node *input_currentNode = (struct bp_node *)((void *)currentNode - pmem);
				//==================================================
				//GET_PATH
				pthread_rwlock_wrlock(&currentNode->rwLock);
				//==================================================

				index = nextNodeIndex_search(input_currentNode, key);

				if(currentNode->is_leaf == TRUE) {
						break;
				}

				//==================================================
				//PUT_PATH
				pthread_rwlock_unlock(&currentNode->rwLock);
				//==================================================

				currentNode = currentNode->children[index];
		}

		struct bp_node *input_currentNode = (struct bp_node *)((void *)currentNode - pmem);

		char *find_key = (void *)currentNode->index[index] + (size_t)pmem;
		char *find_value = (void *)currentNode->children[index] + (size_t)pmem;

		if(strcmp(find_key, key) != 0) {
				//==================================================
				//PUT_PATH
				pthread_rwlock_unlock(&currentNode->rwLock);
				//==================================================
				return -1;
		}

		else {
				removeValue(input_currentNode, index) ;
				//==================================================
				//PUT_PATH
				pthread_rwlock_unlock(&currentNode->rwLock);
				//==================================================
				return 0;
		}
}

//SYSCALL sys_btree_insert()
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
				//				pthread_rwlock_wrlock(&rwLock);

				char *key = malloc(sizeof(char)*256);
				char *value = malloc(sizeof(char)*256);

				sprintf(key, "key_%d", array[i]);
				sprintf(value, "value_%d", array[i]);

				//pthread_rwlock_wrlock(&rwLock);

				char *input_key = alloc_string(strlen(key));
				char *input_value = alloc_string(strlen(value));

				//pthread_rwlock_unlock(&rwLock);

				strcpy(input_key, key);
				strcpy(input_value, value);
				free(key);
				free(value);

				input_key = (char *)((void *)input_key - pmem);
				input_value = (char *)((void *)input_value - pmem);

				int ret = bp_insert(input_key, input_value);
				init_recov();

				if(ret >= 0) {
						count++;
				}
				//				pthread_rwlock_unlock(&rwLock);
		}
		printf("insert count: %d\n", count);
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
				//pthread_rwlock_wrlock(&rwLock);

				char *key = malloc(sizeof(char) * 256);
				sprintf(key, "key_%d",  array[i]);


				int ret = bp_search(key);

				free(key);

				if(ret >= 0) {
						count++;
				}
				//	pthread_rwlock_unlock(&rwLock);
		}
		printf("searchTest count: %d\n", count);
}

void removeTest() {
		//srand(time(NULL));
		int i = 0; int count = 0;
		int j = 0;

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

		for ( i = 0 ; i < testLimit; i++) {
				//	pthread_rwlock_wrlock(&rwLock);
				char *key = malloc(sizeof(char)*256);
				sprintf(key, "key_%d", array[i]);


				int ret = bp_remove(key);

				free(key);

				if(ret >= 0)
						count++;

				//	pthread_rwlock_unlock(&rwLock);
		}
		printf("removeTest count: %d\n", count);
}

void randomInsert(char *key, char *value) {
		//	pthread_rwlock_wrlock(&rwLock);
		//printf("\ninsert start %s %d\n", key, getpid());
		size_t key_len = strlen(key)+1;
		size_t value_len = strlen(value)+1;

		char *input_key = alloc_string(strlen(key));
		char *input_value = alloc_string(strlen(value));

		strcpy(input_key, key);
		strcpy(input_value, value);

		input_key = (char *)((void *)input_key - pmem);
		input_value = (char *)((void *)input_value - pmem);

		int ret = bp_insert(input_key, input_value);
		init_recov();

		//printf("insert: %s %d\n", key, getpid());
		//	pthread_rwlock_unlock(&rwLock);
}

void randomSearch(char *key) {
		//	pthread_rwlock_rdlock(&rwLock);
		printf("\nsearch start %s %d\n", key, getpid());
		size_t key_len = strlen(key)+1;

		char *ret_value = malloc(sizeof(char)*4096);


		int ret = bp_search(key);


		if(ret < 0) 
				printf("search %s %d fail\n", key, getpid());
		else 
				printf("search: %s %d\n", key, getpid());

		//	pthread_rwlock_unlock(&rwLock);
}

void randomRemove(char *key) {

		//	pthread_rwlock_wrlock(&rwLock);
		printf("\nremove start %s %d\n", key, getpid());

		size_t key_len = strlen(key)+1;


		int ret = bp_remove(key);

		if(ret < 0) 
				printf("remove %s %d fail\n", key, getpid());
		else 
				printf("remove SUCCESS %s %d\n", key, getpid());

		//	pthread_rwlock_unlock(&rwLock);
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
								randomInsert(key, value);
								//randomSearch(key);
								break;
						case 2:
								randomInsert(key, value);
								//randomRemove(key);
								break;
				}

				free(key); free(value);
				count++;

				if(count>nr_test)
						break;
		}

		pmem_persist(pmem, PMEM_CAP);
}

void syncTest() {
		int pidLimit = 5;
		pid_t pids[pidLimit];
		int runProcess = 0;
		int state;

		while(runProcess < pidLimit) {
				pids[runProcess] = fork();

				if(pids[runProcess] == 1) {
						process_test();
				}
				else {
						process_test();
				}
				runProcess++;
		}
		printf("%d FINISH!\n", getpid());
}
