#include <stdio.h>

#include "btree.h"
#include "info.h"

int show_info(void *pmem, void **meta, void *jour_area, 
				struct BTreeNode *root, struct free_list *BTreeNode_list, 
				struct free_list *KeyValue_list, struct free_list *data_arr, 
				char *next_string, char *next_struct) {
		int i = 0;

		printf("PMEM_CAP:       %llx\n", PMEM_CAP);
		printf("JOUR_CAP:       %llx\n", JOUR_CAP);
		printf("pmem:           %llx\n", pmem); 
		printf("jour:           %llx\n", jour_area);
		printf("meta:           %llx\n", meta);
		printf("root:           %llx\n", root);
		printf("next_string :   %llx\n", next_string);
		printf("next_struct :   %llx\n", next_struct);
		printf("BTreeNode_list: %llx\n", BTreeNode_list);
		printf("KeyValue_list:  %llx\n", KeyValue_list);
		printf("data_arr :      %llx\n", data_arr);

		for(i = 0; i < 10; i++) {
				printf("meta[%d]:  %10llx ", i, meta[i]);
				printf("&: %llx\n", &meta[i]);
		}

		return 0;
}
