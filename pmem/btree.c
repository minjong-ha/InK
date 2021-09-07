/*
TODO: consider backup mechanism, insert backup_process() func between lines where it need.
	  Implement Recovery_process() memcpy parts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "btree.h" 
#include "info.h"


pthread_rwlock_t rwLock;

int testLimit = 500000;

//static size_t PMEM_CAP;

static void *pmem; static void **meta;


//for new tree structure
static struct BTreeNode *root;
static struct free_list *BTreeNode_list;
static struct free_list *KeyValue_list;
static struct free_list *data_arr;
static char *next_string;
static void *next_struct;

//jour_area start addr
static void *jour_addr;
static void *jour_curr;

long long unsigned int pmem_init() {
		printf("\n=====PMEM_INIT=====\n");

		pmem = malloc(sizeof(char) * (PMEM_CAP + JOUR_CAP));
		meta = (void **)pmem;
		printf("pmem: %llx\n", pmem);

		jour_addr = (void *)PMEM_CAP + (size_t)pmem + 16;
		jour_curr = jour_addr;

		printf("jour_addr: %llx\n", jour_addr);

		int i = 0;
		/*
		   meta[0] : root_node
		   meta[1] : next_string
		   meta[2] : next_struct
		   meta[3] : jour_addr offset
		   meta[4] : jour_curr offset
		   meta[5] : BTreeNode_list
		   meta[8] : KeyValue_list
		   meta[11] : data_arr[]
		 */
		meta[1] = (void *) (1024L * 1024L * 1024L * 2L);
		meta[2] = (void *) PMEM_CAP;

		meta[3] = (void *)jour_addr - (size_t)pmem;
		meta[4] = (void *)jour_curr - (size_t)pmem;
		printf("meta[3]: %llx\n", meta[3]);

		next_string = meta[1] + (size_t)pmem;
		next_struct = meta[2] + (size_t)pmem;

		BTreeNode_list = (struct free_list *)&meta[5];
		KeyValue_list = (struct free_list *)&meta[8];
		BTreeNode_list->head = NULL;
		BTreeNode_list->tail = NULL;
		BTreeNode_list->nr_node = 0;
		KeyValue_list->head  = NULL;
		KeyValue_list->tail  = NULL;
		KeyValue_list->nr_node = 0;

		data_arr = (struct free_list *)&meta[11];
		for(i = 0; i < 256; i++) {
				data_arr[i].head = NULL;
				data_arr[i].tail = NULL;
				data_arr[i].nr_node = 0;
		}

		struct BTreeNode *root_offset = BTreeNode_create();
		meta[0] = (void *)root_offset;
		root = (void *)meta[0] + (size_t)pmem;

		//show_info(pmem, meta, jour_area, root, BTreeNode_list, KeyValue_list, data_arr, next_string, next_struct);

		return (long long unsigned int)pmem;
}

long long unsigned int pmem_load() {
		printf("\n=====PMEM_LOAD=====\n");
		printf("meta[3]: %llx\n", meta[3]);

		recovery_process();

		int i = 0;

		root           = NULL;
		next_string    = NULL;
		next_struct    = NULL;
		BTreeNode_list = NULL;
		KeyValue_list  = NULL;
		data_arr       = NULL;

		meta           = (void **)pmem;
		root         	 = meta[0] + (size_t)pmem;
		next_string 	 = meta[1] + (size_t)pmem;
		next_struct    = meta[2] + (size_t)pmem;
		jour_addr      = meta[3] + (size_t)pmem;
		jour_curr      = meta[4] + (size_t)pmem;
		BTreeNode_list = (struct free_list *)&meta[5];
		KeyValue_list  = (struct free_list *)&meta[8];
		data_arr       = (struct free_list *)&meta[11];

		printf("jour_addr: %llx, jour_curr: %llx\n", jour_addr, jour_curr);

		//show_info(pmem, meta, jour_area,  root, BTreeNode_list, KeyValue_list, data_arr, next_string, next_struct);

		return (long long unsigned int)pmem;

}

int pmem_exit() {
		printf("\n=====PMEM_EXIT=====\n");
		printf("jour_curr: %d\n", jour_curr-jour_addr);
		free(pmem);
		return 0;
}

//create BTreeNode
struct BTreeNode *BTreeNode_create() {

		struct BTreeNode *new_node = NULL;
		new_node = realloc_BTreeNode();

		int i = 0;
		for(i=0; i < ARRAY_NODE; i++) {
				new_node->chunks[i] = NULL;
		}
		for(i=0; i < ARRAY_CHILDREN; i++) {
				new_node->children[i] = NULL;
		}
		new_node->nr_chunks = 0;

		new_node = (struct BTreeNode *)((void *)new_node - pmem);

		return new_node;
}

//create KeyValue chunk
//key, value are OFFSETs.
struct KeyValue *KeyValue_create(char *key_offset, char *value_offset) {
		struct KeyValue *new_kv = NULL;
		new_kv = realloc_KeyValue();

		new_kv->key = key_offset;
		new_kv->value = value_offset;

		new_kv = (struct KeyValue *)((void *)new_kv - pmem);

		return new_kv;
}

//duplicate KeyValue chunk
struct KeyValue *KeyValue_duplicate(struct KeyValue *input_kv) {
		struct KeyValue *kv = (void *)input_kv + (size_t)pmem;
		char *key = kv->key + (size_t)pmem;
		char *value = kv->value + (size_t)pmem;

		size_t key_len = strlen(key);
		size_t value_len = strlen(value);

		int index = 0;

		char *new_key = NULL;
		index = key_len/16;
		new_key = realloc_string(key_len);

		char *new_value = NULL;
		index = value_len/16;
		new_value = realloc_string(value_len);

		strcpy(new_key, key);
		strcpy(new_value, value);

		new_key = (char *)((void *)new_key - pmem);
		new_value = (char *)((void *)new_value - pmem);
		struct KeyValue *new_kv = KeyValue_create(new_key, new_value);

		return new_kv;
}

//basic operations
//left, right for split
void addValue(struct BTreeNode *input_node, struct KeyValue *input_kv, struct BTreeNode *input_left, struct BTreeNode *input_right){
		struct BTreeNode *node  = (void *)input_node + (size_t)pmem;
		struct KeyValue *kv    = (void *)input_kv + (size_t)pmem;
		struct BTreeNode *left = (void *)input_left + (size_t)pmem;
		struct BTreeNode *right = (void *)input_right + (size_t)pmem;

		char *kv_key = kv->key + (size_t)pmem;
		char *kv_value = kv->value + (size_t)pmem;

		if(input_left == NULL){
				left = NULL;
		}
		if(input_right == NULL){
				right = NULL;
		}

     	//recovery need======================================================================
		backup_process(input_node, sizeof(struct BTreeNode));
		//============================================================

		//insertion_sort(feat.memcpy)
		node->chunks[node->nr_chunks] = NULL;
		node->children[node->nr_chunks + 1] = NULL;

		int i = nextNodeIndex(input_node, kv->key);
		changeOffset(&node->chunks[i+1], &node->chunks[i], sizeof(struct KeyValue *) * (node->nr_chunks - i));
		changeOffset(&node->children[i+2], &node->children[i+1], sizeof(struct BTreeNode *) * (node->nr_chunks - i));

		node->nr_chunks++;
		node->chunks[i] = input_kv;
		node->children[i] = input_left;
		node->children[i+1] = input_right;

		//printf("KEY: %s\tVALUE: %s\t added!\n", kv_key, kv_value);
}

//removeValue
void removeValue(struct BTreeNode *input_node, int index) {
		struct BTreeNode *node = (void *)input_node + (size_t)pmem;
		struct KeyValue *node_chunks = (void *)node->chunks[index] + (size_t)pmem;

		//backup need
		char *backup_key, *backup_value;
		backup_key = (void *)node_chunks->key + (size_t)pmem;
		backup_value = (void *)node_chunks->value + (size_t)pmem;
		backup_process(node_chunks->key, strlen(backup_key));
		backup_process(node_chunks->value, strlen(backup_value));
		backup_process(input_node, sizeof(struct BTreeNode));
		//-==========================================

		recall_string(node_chunks->key);
		recall_string(node_chunks->value);
		recall_KeyValue(node->chunks[index]);
		node->chunks[index] = NULL;

		recall_BTreeNode(node->children[index]);
		node->children[index] = NULL;

		if(index <= node->nr_chunks - 1) {
				changeOffset(&node->chunks[index], &node->chunks[index+1], sizeof(struct KeyValue *) * (node->nr_chunks - index - 1));
				changeOffset(&node->children[index], &node->children[index+1], sizeof(struct BTreeNode *) * (node->nr_chunks - index));

				node->chunks[node->nr_chunks-1] = NULL;
				node->children[node->nr_chunks] = NULL;
		}
		node->nr_chunks--;
}

//node, key are OFFSET
//return index in node
int findValue(struct BTreeNode *input_node, char *input_key) {
		struct BTreeNode *node = (void *)input_node + (size_t)pmem;
		char *key = input_key + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		while(low <= high) {
				int mid = (low + high) / 2;

				struct KeyValue *node_chunks_mid = (void *)node->chunks[mid] + (size_t)pmem;
				char *node_chunks_mid_key = node_chunks_mid->key + (size_t)pmem;

				if (strcmp(key, node_chunks_mid_key) == 0) {
						return mid;
				}
				else {
						if (strcmp(key, node_chunks_mid_key) > 0)
								low = mid + 1;
						else
								high = mid - 1;
				}
		}

		return -1;
}

//node OFFSET
//input_key is not OFFSET!
//return index in node
int findValue_search(struct BTreeNode *input_node, char *input_key) {
		struct BTreeNode *node = (void *)input_node + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		while(low <= high) {
				int mid = (low + high) / 2;

				struct KeyValue *node_chunks_mid = (void *)node->chunks[mid] + (size_t)pmem;
				char *node_chunks_mid_key = (void *)node_chunks_mid->key + (size_t)pmem;

				if (strcmp(input_key, node_chunks_mid_key) == 0) {
						return mid;
				}
				else {
						if (strcmp(input_key, node_chunks_mid_key) > 0)
								low = mid + 1;
						else
								high = mid - 1;
				}
		}

		return -1;
}

//input_node, input_key are OFFSET
int nextNodeIndex (struct BTreeNode *input_node, char *input_key) {
		struct BTreeNode *node = (void *)input_node + (size_t)pmem;
		char *key              = input_key  + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks-1;

		while(low <= high) {
				int mid = (low + high) / 2;

				struct KeyValue *node_chunks = (void *)node->chunks[mid] + (size_t)pmem;
				char *node_chunks_key = node_chunks->key + (size_t)pmem;

				if (strcmp(key, node_chunks_key) > 0)
						low = mid + 1;
				else
						high = mid - 1;
		}

		if (low == high)
				return low+1;
		else
				return low;

}

//input_node is OFFSET
//input_key is not OFFSET!
int nextNodeIndex_search (struct BTreeNode *input_node, char *input_key) {
		struct BTreeNode *node = (void *)input_node + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks-1;

		while(low <= high) {
				int mid = (low + high) / 2;

				struct KeyValue *node_chunks = (void *)node->chunks[mid] + (size_t)pmem;
				char *node_chunks_key = node_chunks->key + (size_t)pmem;

				if (strcmp(input_key, node_chunks_key) > 0)
						low = mid + 1;
				else
						high = mid - 1;
		}

		if (low == high)
				return low+1;
		else
				return low;

}

void clearNode(struct BTreeNode *input_node) {
		struct BTreeNode *node = (void *)input_node + (size_t)pmem;

		int i = 0;
		for (i = 0 ; i < ARRAY_NODE ; i++) {
				node->chunks[i] = NULL;
		}
		for (i = 0 ; i < ARRAY_CHILDREN ; i++) {
				node->children[i] = NULL;
		}
		node->nr_chunks = 0;
}

void clearKV(struct KeyValue *input_kv) {
		struct KeyValue *kv = (void *)input_kv + (size_t)pmem;
		kv->key = NULL;
		kv->value = NULL;
}

void changeOffset(void *dest, void *src, size_t size) {
		void *mem_node = malloc(sizeof(void *) * (size));

		memcpy(mem_node, src, size);
		memset(dest, 0, size);
		memcpy(dest, mem_node, size);

		free(mem_node);
}

void recall_BTreeNode(struct BTreeNode *input_node) {
		if(input_node == NULL) {
				//do nothing
		}
		else {
				clearNode(input_node);
				struct free_node *fn = (void *)input_node;
				struct free_node *tmp_fn = (void *)fn + (size_t)pmem;
				tmp_fn->next = NULL;

				//backup need
				void *list_offset = (struct free_list *)((void *)BTreeNode_list - pmem);
				backup_process(BTreeNode_list->head, sizeof(struct free_node));
				backup_process(BTreeNode_list->tail, sizeof(struct free_node));
				backup_process(list_offset, sizeof(struct free_list));

				if(BTreeNode_list->nr_node == 0) {
						BTreeNode_list->head = (void *)fn;
						BTreeNode_list->tail = (void *)fn;
						BTreeNode_list->nr_node++;
				}
				else {
						struct free_node *tail_fn = (void *)BTreeNode_list->tail + (size_t)pmem;
						tail_fn->next = (void *)fn;
						BTreeNode_list->tail = (void *)fn;
						BTreeNode_list->nr_node++;
				}
		}
}

struct BTreeNode *realloc_BTreeNode() {
		struct BTreeNode *ret = NULL;

		//recovery needed
		//backup_process(BTreeNode_list, sizeof(struct free_list));
		void *list_offset = (struct free_list *)((void *)BTreeNode_list - pmem);
		backup_process(BTreeNode_list->head, sizeof(struct free_node));
		backup_process(BTreeNode_list->tail, sizeof(struct free_node));
		backup_process(list_offset, sizeof(struct free_list));
		//-====================

		if(BTreeNode_list->nr_node > 0) {
				struct free_node *fn = (void *)BTreeNode_list->head + (size_t)pmem;
				BTreeNode_list->head = fn->next;
				ret = (void *)fn;
				BTreeNode_list->nr_node--;
		}

		else {
				next_struct = next_struct - sizeof(struct BTreeNode);
				meta[2]     = meta[2] - sizeof(struct BTreeNode);
				ret = next_struct;
		}

		return ret;
}

void recall_KeyValue(struct KeyValue *input_kv) {
		if(input_kv == NULL) {
		}
		else {
				clearKV(input_kv);
				struct free_node *fn = (void *)input_kv;
				struct free_node *tmp_fn = (void *)fn + (size_t)pmem;
				tmp_fn->next = NULL;

				//recovery
				//backup_process(KeyValue_list, sizeof(struct free_list));
				void *list_offset = (struct free_list *)((void *)KeyValue_list - pmem);
				backup_process(KeyValue_list->head, sizeof(struct free_node));
				backup_process(KeyValue_list->tail, sizeof(struct free_node));
				backup_process(list_offset, sizeof(struct free_list));
				//-----------------

				if (KeyValue_list->nr_node == 0) {
						KeyValue_list->head = (void *)fn;
						KeyValue_list->tail = (void *)fn;
						KeyValue_list->nr_node++;
				}
				else {
						struct free_node *tail_fn = (void *)KeyValue_list->tail + (size_t)pmem;
						tail_fn->next = (void *)fn;
						KeyValue_list->tail = (void *)fn;
						KeyValue_list->nr_node++;
				}
		}
}

struct KeyValue *realloc_KeyValue() {
		struct KeyValue *ret = NULL;

		//backup_process(KeyValue_list, sizeof(struct free_list));
		void *list_offset = (struct free_list *)((void *)KeyValue_list - pmem);
		backup_process(KeyValue_list->head, sizeof(struct free_node));
		backup_process(KeyValue_list->tail, sizeof(struct free_node));
		backup_process(list_offset, sizeof(struct free_list));

		if(KeyValue_list->nr_node > 0) {
				struct free_node *fn = (void *)KeyValue_list->head + (size_t)pmem;
				KeyValue_list->head = fn->next;
				ret = (void *)fn;
				KeyValue_list->nr_node--;
		}
		else {
				next_struct = next_struct - sizeof(struct KeyValue);
				meta[2] = meta[2] - sizeof(struct KeyValue);
				ret = next_struct;
		}

		return ret;
}

void recall_string(char *input_string) {
		if(input_string == NULL) {
				//do nothing
		}
		else {
				char *string = (void *)input_string + (size_t)pmem;
				int index = strlen(string)/16;
				string = NULL;
				int i = 0;

				struct free_node *fn = (void *)input_string;
				struct free_node *tmp_fn = (void *)fn + (size_t)pmem;
				tmp_fn->next = NULL;

				//recovery needed
				//backup_process(&data_arr[index], sizeof(struct free_list));
				void *list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
				backup_process(data_arr[index].head, sizeof(struct free_node));
				backup_process(data_arr[index].tail, sizeof(struct free_node));
				backup_process(list_offset, sizeof(struct free_list));
				//==================

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
}

char *realloc_string(size_t length) {
		char *ret = NULL;
		int index = length/16;

		//recovery need
		//backup_process(&data_arr[index], sizeof(struct free_list));
		void *list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
		backup_process(data_arr[index].head, sizeof(struct free_node));
		backup_process(data_arr[index].tail, sizeof(struct free_node));
		backup_process(list_offset, sizeof(struct free_list));
		//=====================

		if(data_arr[index].nr_node > 0) {
				struct free_node *fn = (void *)data_arr[index].head + (size_t)pmem;
				data_arr[index].head = fn->next;
				ret = (void *)fn;
				data_arr[index].nr_node--;
		}
		else if(data_arr[index].nr_node == 0) {
				ret = next_string;
				meta[1] = meta[1] + ((index + 1) * 16);
				next_string = next_string + ((index + 1) * 16);
		}

		return ret;
}


//split full normal node(currentNode)
//return parentNode
struct BTreeNode *BTreeNode_splitnormal(char *input_key, struct BTreeNode *input_parentNode) {
		char *key = (void *)input_key + (size_t)pmem;
		struct BTreeNode *parentNode = (void *)input_parentNode + (size_t)pmem;

		struct BTreeNode *left, *right;
		struct BTreeNode *split; // return node
		int i;

		i = nextNodeIndex(input_parentNode, input_key);

		//recovery bakcup needed
		backup_process(input_parentNode, sizeof(struct BTreeNode));
		backup_process(parentNode->children[i], sizeof(struct BTreeNode));
		//======================

		left = parentNode->children[i];
		left = (void *)left + (size_t)pmem;
		left->nr_chunks = ARRAY_NODE/2;

		right = BTreeNode_create();
		right = (void *)right + (size_t)pmem;

		changeOffset(&right->chunks[0], &left->chunks[(ARRAY_NODE/2)+1], sizeof(struct KeyValue *) * (ARRAY_NODE - ((ARRAY_NODE/2)+1)));
		changeOffset(&right->children[0], &left->children[(ARRAY_NODE/2)+1], sizeof(struct BTreeNode *) * (ARRAY_NODE - ((ARRAY_NODE/2)+1) + 1));
		right->nr_chunks = ARRAY_NODE/2;

		struct KeyValue *temp = left->chunks[ARRAY_NODE/2];
		left->chunks[ARRAY_NODE/2] = NULL;
		left = (struct BTreeNode *)((void *)left - pmem);
		right = (struct BTreeNode *)((void *)right - pmem);
		addValue(input_parentNode, temp, left, right);
		split = parentNode;

		struct BTreeNode *offset_split = (struct BTreeNode *)((void *)split - pmem);

		return offset_split;
}

//split full root node
//return root (parent)
struct BTreeNode *BTreeNode_splitroot(struct BTreeNode *input_parentNode) {
		struct BTreeNode *parentNode = (void *)input_parentNode + (size_t)pmem;

		struct BTreeNode *left, *right;
		struct BTreeNode *split; // return node

		split = parentNode;

		//=====
		//backup need
		backup_process(input_parentNode, sizeof(struct BTreeNode));
		//=====

		//distribute to left
		left = BTreeNode_create();
		left = (void *)left + (size_t)pmem;

		changeOffset(&left->chunks[0], &split->chunks[0], sizeof(struct KeyValue *) * (ARRAY_NODE/2));
		changeOffset(&left->children[0], &split->children[0], sizeof(struct BTreeNode *) * ((ARRAY_NODE/2) + 1));

		left->nr_chunks = ARRAY_NODE/2;

		//distribute to right
		right = BTreeNode_create();
		right = (void *)right + (size_t)pmem;

		changeOffset(&right->chunks[0], &split->chunks[(ARRAY_NODE/2)+1], sizeof(struct KeyValue *) * (ARRAY_NODE - ((ARRAY_NODE/2)+1)));
		changeOffset(&right->children[0], &split->children[(ARRAY_NODE/2)+1], sizeof(struct BTreeNode *) * (ARRAY_NODE - ((ARRAY_NODE/2)+1) + 1));
		right->nr_chunks = ARRAY_NODE/2;

		struct KeyValue *temp = split->chunks[ARRAY_NODE/2];
		struct BTreeNode *input_split = (struct BTreeNode *)((void *)split - pmem);
		left = (struct BTreeNode *)((void *)left - pmem);
		right = (struct BTreeNode *)((void *)right - pmem);
		clearNode(input_split);
		addValue(input_split, temp, left, right); 

		return input_parentNode;
}

//swap value
//for remove. Remove operations are removed in leaf
//return actual string pointer, not OFFSET.
char *swapValue(struct BTreeNode *input_willDelete, int index) {
		struct BTreeNode *willDelete = (void *)input_willDelete + (size_t)pmem;
		struct BTreeNode *temp = NULL;
		struct BTreeNode *parent = NULL;

		parent = willDelete;

		//moving right child
		temp = (void *)parent->children[index + 1]+ (size_t)pmem;

		//moving smallest next value
		while(temp->children[0] != NULL) {
				parent = temp;
				temp = (void *)temp->children[0] + (size_t)pmem;
		}

		//backup_need
		backup_process(temp->chunks[0], sizeof(struct KeyValue));
		backup_process(input_willDelete, sizeof(struct BTreeNode));

		struct KeyValue *kv = KeyValue_duplicate(temp->chunks[0]);

		struct KeyValue *true_kv = (void *)kv + (size_t)pmem;
		char *kv_key = (void *)true_kv->key + (size_t)pmem;

		struct KeyValue *willDelete_chunks = (void *)willDelete->chunks[index] + (size_t)pmem;

		//backup_need
		char *backup_key, *backup_value;
		backup_key = (void *)willDelete_chunks->key + (size_t)pmem;
		backup_value = (void *)willDelete_chunks->value + (size_t)pmem;
		backup_process(willDelete_chunks->key, strlen(backup_key));
		backup_process(willDelete_chunks->value, strlen(backup_value));

		recall_string(willDelete_chunks->key);
		recall_string(willDelete_chunks->value);
		recall_KeyValue(willDelete->chunks[index]);
		willDelete_chunks = NULL;

		willDelete->chunks[index] = kv;

		return kv_key;
}

int BTree_search(char *key) {
		struct BTreeNode *currentNode;
		int index;

		currentNode = (struct BTreeNode *)((void *)root - pmem);

		while(currentNode != 0) {
				currentNode = (void *)currentNode + (size_t)pmem;
				struct BTreeNode *currentNode_offset = (struct BTreeNode *)((void *)currentNode - pmem);

				index = findValue_search(currentNode_offset, key);
				if(index != -1) {
						break;
				}
				if(index < 0) {
						int i = 0;
						i = nextNodeIndex_search(currentNode_offset, key);
						currentNode = currentNode->children[i];
				}
		}

		//not found
		if(currentNode == 0) {
				//printf("not found %s\n", key);
				return -1;
		}

		//found!
		struct KeyValue *find_kv = (void *)currentNode->chunks[index] + (size_t)pmem;
		char *find_key = find_kv->key + (size_t)pmem;
		char *find_value = find_kv->value + (size_t)pmem;
		//printf("found %s %s\n", find_key, find_value);

		return 0;
}



//kv is OFFSET!
int BTree_insert(struct KeyValue *input_kv) {
		struct KeyValue *kv = (void *)input_kv + (size_t)pmem;
		char *kv_key = kv->key + (size_t)pmem;
		char *kv_value = kv->value + (size_t)pmem;

		struct BTreeNode *currentNode, *parentNode;
		int i;

		parentNode = root;
		currentNode = parentNode;
		currentNode = (struct BTreeNode *)((void *)currentNode - pmem);

		while(currentNode != 0) {
				currentNode = (void *)currentNode + (size_t)pmem;
				struct BTreeNode *currentNode_offset = (struct BTreeNode *)((void *)currentNode - pmem);
				struct BTreeNode *input_parentNode = (struct BTreeNode *)((void *)parentNode - pmem);
				int ret_findValue = findValue(currentNode_offset, kv->key);

				//update
				if(ret_findValue >= 0) {
						struct KeyValue *tmp_kv = (void *)currentNode->chunks[ret_findValue] + (size_t)pmem;
//						char *tmp_key = (void *)tmp_kv->key + (size_t)pmem;
						char *tmp_value = (void *)tmp_kv->value + (size_t)pmem;
//						recall_string(tmp_kv->key);
//						recall_string(tmp_kv->value);
//						recall_KeyValue(currentNode->chunks[ret_findValue]);
//						currentNode->chunks[ret_findValue] = input_kv;

						//=====
						//copy tmp_kv->value and recover it.
						backup_process(tmp_kv->value, strlen(tmp_value));
						backup_process(currentNode->chunks[ret_findValue], sizeof(struct KeyValue));

						recall_string(tmp_kv->value);
						tmp_kv->value = kv->value;

						recall_string(kv->key);
						recall_KeyValue(input_kv);
						//=====

						return 0;
				}
				if(currentNode->nr_chunks == ARRAY_NODE) {
						if(currentNode == root) {

								currentNode = BTreeNode_splitroot(input_parentNode);
								currentNode = (void *)currentNode + (size_t)pmem;
						}
						else {
								currentNode = BTreeNode_splitnormal(kv->key, input_parentNode);
								currentNode = (void *)currentNode + (size_t)pmem;
						}
				}

				parentNode = currentNode;
				input_parentNode = (struct BTreeNode *)((void *)parentNode - pmem);
				i = nextNodeIndex(input_parentNode, kv->key);
				currentNode = currentNode->children[i];
		}

		struct BTreeNode *input_parentNode = (struct BTreeNode *)((void *)parentNode - pmem);
		addValue(input_parentNode, input_kv, NULL, NULL);

		return 0;
}

//key is not offset
//its just string
int BTree_remove(char *key) {

		struct BTreeNode *grand, *parent, *current;
		int parentIndex = 0; int currentIndex = 0; int grandIndex = 0;
		char *search = key;

		parent = root;
		current = parent;

		current = (struct BTreeNode *)((void *)current - pmem);

		while(current != NULL) {
				int isSwap = 0;
				current = (void *)current + (size_t)pmem;
				struct BTreeNode *input_current = (struct BTreeNode *)((void *)current - pmem);
				struct BTreeNode *input_parent = (struct BTreeNode *)((void *)parent - pmem);
				currentIndex = findValue_search(input_current, search);

				if(currentIndex >= 0) {

						//find value
						if(current->children[0] == NULL) {
								//leafNode
								break;
						}
						else {
								//innerNode
								search = swapValue(input_current, currentIndex);
								isSwap = 1;
						}
				}

				grand = parent;
				grandIndex = parentIndex;
				parent = current;
				input_parent = (struct BTreeNode *)((void *)parent - pmem);
				parentIndex = nextNodeIndex_search(input_parent, search);

				if(isSwap == 1) {
						parentIndex = parentIndex + 1;
				}

				current = current->children[parentIndex];
		}

		if(current == NULL) {
				//printf("KEY: %s\t not found! can't remove!\n", search);
				return -1;
		}


		//is current offset? i think so...
		struct BTreeNode *input_current = (struct BTreeNode *)((void *)current - (size_t)pmem);
		int index = findValue_search(input_current, search);
		removeValue(input_current, index);
		//printf("KEY: %s\t removed\n", search);


		//clean empty leaf node
		if(current->nr_chunks == 0 && parent->nr_chunks > 1) {
				if(parentIndex == parent->nr_chunks) {
						//rightmost child empty

						//BACKUP parent->chunks[parentIndex - 1] ====> OFFSET;OFFSET;OFFSET;....
						struct KeyValue *kv = parent->chunks[parentIndex - 1];

						recall_BTreeNode(parent->children[parentIndex]);
						parent->children[parentIndex] = NULL;
						parent->chunks[parentIndex - 1] = NULL;
						parent->nr_chunks--;

						int ret = BTree_insert(kv);
						return 1;

				}
				else {
						//internal child empty

						//instead duplicate, just use offset
						struct KeyValue *kv = KeyValue_duplicate(parent->chunks[parentIndex]);

						struct BTreeNode *input_parent = (struct BTreeNode *)((void *)parent - pmem);
						//no remove, just reordering. (TOTAL REFACTORING NEEDED)
						removeValue(input_parent, parentIndex);

						int ret = BTree_insert(kv);
						return 2;
				}
		}

		else if(current->nr_chunks == 0 && parent->nr_chunks == 1) {

				if(parent == root) {
						//empty root
						recall_BTreeNode(parent->children[parentIndex]);
						parent->children[parentIndex] = NULL;

						int index;
						if(parentIndex == 0)
								index = 1;
						else 
								index = 0;

						struct BTreeNode *tmp = parent;
						meta[0] = (void *)parent->children[index];
						root = (void *)meta[0] + (size_t)pmem;

						int i = 0;
						for(i = 0 ; i < tmp->nr_chunks; i++) {
								int ret = BTree_insert(tmp->chunks[i]);
						}

						return 3;

				}

				else {
						//empty normal node
						recall_BTreeNode(parent->children[parentIndex]);
						parent->children[parentIndex] = NULL;

						int index;
						if(parentIndex == 0 )
								index = 1;
						else
								index = 0;	

						struct BTreeNode *tmp = parent;
						grand->children[grandIndex] = parent->children[index];

						int i = 0;
						for(i = 0; i < tmp->nr_chunks; i++) {
								int ret = BTree_insert(tmp->chunks[i]);
						}
						struct BTreeNode *input_tmp = (struct BTreeNode *)((void *)tmp - pmem);
						recall_BTreeNode(input_tmp);

						return 4;
				}

		}

		else if(current->nr_chunks == 0 && parent->nr_chunks == 0) {
				//do nothing
				return 0;
		}

		return 0;
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
				pthread_rwlock_wrlock(&rwLock);
				char *key = malloc(sizeof(char)*256);

				sprintf(key, "key_%d", array[i]);

				int ret = BTree_remove(key);

				//IT SHOULD BE LOCATED AFTER SYSCALL COMPLETE.
				//memset jour_curr, and jour_addr
				memset(jour_addr, 0, jour_curr - jour_addr);
				jour_curr = jour_addr;

				if(ret >= 0)
						count++;

				free(key);
				pthread_rwlock_unlock(&rwLock);
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
				pthread_rwlock_rdlock(&rwLock);
				char *key = malloc(sizeof(char) * 256);

				sprintf(key, "key_%d",  array[i]);

				size_t key_len = strlen(key);

				int ret = BTree_search(key);

				if(ret >= 0) {
						count++;
				}
				free(key);
				pthread_rwlock_unlock(&rwLock);
		}
		printf("searchTest count: %d\n", count);
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
				pthread_rwlock_wrlock(&rwLock);

				char *key = malloc(sizeof(char)*256);
				char *value = malloc(sizeof(char)*256);

				sprintf(key, "key_%d", array[i]);
				sprintf(value, "value_%d", array[i]);

				char *input_key = NULL;
				input_key = realloc_string(strlen(key));

				char *input_value = NULL;
				input_value = realloc_string(strlen(value));

				strcpy(input_key, key);
				strcpy(input_value, value);
				free(key);
				free(value);

				input_key = (char *)((void *)input_key - pmem);
				input_value = (char *)((void *)input_value - pmem);

				struct KeyValue *kv = KeyValue_create(input_key, input_value);

				int ret = BTree_insert(kv);

				//recovery_process();

				init_recov();

				if(ret >= 0) {
						count++;
				}
				pthread_rwlock_unlock(&rwLock);
		}
		printf("insert count: %d\n", count);

}

void backup_process(void *target_offset, size_t target_size) {
		size_t first_leng;
		size_t second_leng;
		void *target_addr = (void *)target_offset + (size_t)pmem;

		if(target_offset == NULL) {
				//do nothing
		}

		else{

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
						//memcpy(target_aadr, target, length) need;
				}


				free(backup_data);
				free(offset);
				free(length);
				free(target);
		
//				init_recov();
		}
		else {
				//printf("NO NEED TO RECOVERY. EVERYTHING FINE\n");
		}
}

void init_recov() {
		memset(jour_addr, 0, jour_curr - jour_addr);
		jour_curr = jour_addr;
}


void showTree() {
		//root: BTreeNode_root addr

		printf("\n");
		if(root->nr_chunks > 0) {
				for(int i = 0; i < root->nr_chunks; i++) {
						struct KeyValue *tmp_kv = (void *)root->chunks[i] + (size_t)pmem;
						char *tmp_key = (void *)tmp_kv->key + (size_t)pmem;
						char *tmp_value = (void *)tmp_kv->value + (size_t)pmem;
						printf("key: %s\tvalue:%s\n", tmp_key, tmp_value);
				}
				printf("\n");
		}

		if(root->children[0] != NULL) {
				for(int i = 0; i < root->nr_chunks + 1; i++) {
						showNode(root->children[i]);
				}

		}

		printf("SHOW TREE END\n");
}

void showNode(struct BTreeNode *node_offset) {
		struct BTreeNode *node = (void *)node_offset + (size_t)pmem;

		printf("\n");
		for (int i = 0; i < node->nr_chunks; i++) {
				struct KeyValue *tmp_kv = (void *)node->chunks[i] + (size_t)pmem;
				char *tmp_key = (void *)tmp_kv->key + (size_t)pmem;
				char *tmp_value = (void *)tmp_kv->value + (size_t)pmem;
				printf("key: %s\tvalue:%s\n", tmp_key, tmp_value);
		}
		printf("\n");

		if(node->children[0] != NULL) {
				for(int i = 0; i < node->nr_chunks + 1; i++) {
						showNode(node->children[i]);
				}
		}

}

