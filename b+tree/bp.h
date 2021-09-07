#ifndef BP_H_
#define BP_H_

#define TRUE 1
#define FALSE 0

#define INDEX_MAX 255

#define JOUR_CAP (1024L * 1024L * 1024L * 1)
#define PMEM_CAP (1024L * 1024L * 1024L * 4)

struct bp_node {
		char *index[INDEX_MAX];
		void *children[INDEX_MAX + 1];
		int nr_chunks;
		int is_leaf;
		pthread_rwlock_t rwLock;
};

struct free_list {
		struct free_node *head;
		struct free_node *tail;
		int nr_node;
};

struct free_node {
		struct free_node *next;
};

struct bp_node *bp_node_create();

void bp_init();
void bp_load();
void bp_exit();

void backup_process(void *target_offset, size_t target_size);
void recovery_process();
void init_recov();

void clear_bp_node(struct bp_node *input_node);
void free_bp_node(struct bp_node *input_node);
struct bp_node *alloc_bp_node();

void free_string(char *input_string);
char *alloc_string(size_t length);

int findValue(struct bp_node *input_node, char *input_key);
int nextNodeIndex(struct bp_node *input_node, char *input_key);

void changeOffset(void *dest, void *src, size_t size);
void addValue(struct bp_node *input_node, char *input_key, void *input_left, void *input_right);
void removeValue(struct bp_node *input_node, int index);

struct bp_node *bp_node_splitRoot(struct bp_node *input_parentNode);
struct bp_node *bp_node_splitNormal(struct bp_node *input_currentNode, struct bp_node *input_parentNode, char *input_key);

int bp_insert(char *input_key, char *input_value);
int bp_search(char *input_key);
int bp_remove(char *input_key);

void insertTest();
void searchTest();
void removeTest();

void randomInsert(char *key, char *value);
void randomSearch(char *key);
void randomRemove(char *key);
void process_test();
void syncTest();

#endif
