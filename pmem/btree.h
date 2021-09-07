#ifndef BTREE_H_
#define BTREE_H_

#include <stddef.h>
//BTREE FOR ODD ARRAY_NODE
#define ARRAY_NODE 255
#define ARRAY_CHILDREN (ARRAY_NODE+1)

//N GB PMEM SIMULATION
#define JOUR_CAP (1024L * 1024L * 1024L * 1)
#define PMEM_CAP (1024L * 1024L * 1024L * 4)

struct KeyValue {
        char *key; //key, 256 byte
        char *value; //value, 4096 byte
};

struct BTreeNode {
        struct KeyValue *chunks[ARRAY_NODE]; //array of pointers of chunks
        struct BTreeNode *children[ARRAY_CHILDREN]; //array of pointers of children
        int nr_chunks; //current number of chunks, MAX ARRAY_NODE
        
	/*
         * int count = 8 bytes (will be changed to long long or __int64
         * KeyValue *chunks[255] = 8 x 255 bytes (2040)
         * BTreeNode *children[256] = 8 x 256 bytes (2048)
         * 8 + 2040 + 2048 = 4096 bytes
         * linux page allocate 4KB(4096 bytes) (current 4 bytes fragment)
         */
};

struct BTree {
        struct BTreeNode *root; //root node pointer.
        int level;
};

struct free_list {
				struct free_node *head;
				struct free_node *tail;
				int nr_node;
};

struct free_node {
				struct free_node *next;
};

//PMEM simulation
long long unsigned int pmem_init();
long long unsigned int pmem_load();
int pmem_exit();

//Create Nodes and Chunks
struct BTreeNode *BTreeNode_create();
struct KeyValue *KeyValue_create(char *key, char *value);
struct KeyValue *KeyValue_duplicate(struct KeyValue *kv);

//Basic Operations. Single Node operation
void addValue(struct BTreeNode *node, struct KeyValue *kv, struct BTreeNode *left, struct BTreeNode *right);
void removeValue(struct BTreeNode *node, int index);
int findValue(struct BTreeNode *node, char *key);
int nextNodeIndex(struct BTreeNode *node, char *key);
void clearNode(struct BTreeNode *node);

void changeOffset(void *dest, void *src, size_t size);

//Free and Reallocation
void recall_BTreeNode(struct BTreeNode *input_node);
struct BTreeNode *realloc_BTreeNode();

void recall_KeyValue(struct KeyValue *input_kv);
struct KeyValue *realloc_KeyValue();

void recall_string();
char *realloc_string(size_t length);

//For BTree_insert
struct BTreeNode *BTreeNode_splitnormal(char *key, struct BTreeNode *parentNode);
struct BTreeNode *BTreeNode_splitroot(struct BTreeNode *parentNode);

//For BTree_remove
char *swapValue(struct BTreeNode *willDelete, int index);

//Tree operation
int BTree_search(char *key);
int BTree_insert(struct KeyValue *kv);
int BTree_remove(char *key);
void showNode(struct BTreeNode *node);
void showTree();

//Test operation
void removeTest();
void searchTest();
void insertTest();

void backup_process(void *target_offset, size_t target_size);
void recovery_process();
void init_recov();

#endif
