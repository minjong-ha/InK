#ifndef INFO_H_
#define INFO_H_

#include "btree.h"

int show_info(void *pmem, void **meta, void *jour_area, 
				struct BTreeNode *root, struct free_list *BTreeNode_list, 
				struct free_list *KeyValue_list, struct free_list *data_arr, 
				char *next_string, char *next_struct);

#endif
