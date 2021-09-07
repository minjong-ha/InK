#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h> 
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/list.h>
#include <linux/gfp.h>

#include <linux/proc_fs.h>

#include <linux/spinlock.h>
#include <linux/rwsem.h> 
#include <trace/events/sched.h>

#include <linux/kthread.h>

#include <linux/init.h>
#include <linux/fcntl.h>
#include <linux/fs.h>

#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

#include <asm/cacheflush.h>

#include "../drivers/nvdimm/pmem.h"

#define KEY_MAX 256
#define VALUE_MAX 2048

#define INDEX_MAX 63
#define CREATE_TRACE_POINTS

#define TRUE 1
#define FALSE 0

//4GB
#define PMEM_CAP (1024L * 1024L * 1024L * 64L)

//list_rwlock
//static DECLARE_RWSEM(list_rwsem);
static struct rw_semaphore list_rwsem;
static struct rw_semaphore backup_rwsem;

struct bp_node {
		char *index[INDEX_MAX];
		void *children[INDEX_MAX + 1];
		int nr_chunks;
		int is_leaf;
		struct rw_semaphore node_rwsem;
};

struct free_node {
		struct free_node *next;
};

struct free_list {
		struct free_node *head;
		struct free_node *tail;
		unsigned int nr_node;
};

//=====
//static BTree root pointer
static struct bp_node *root;

static void *pmem;
static void **meta;

static void *next_ptr;
static struct free_list *data_arr;

static void *jour_addr;
static void *jour_curr;

static int alloc_count[100];
static int is_alloc;

static int reuse_count;

/*
   meta[0] : root_node
   meta[1] : next_ptr
   meta[2] : jour_addr offset
   meta[3] : jour_curr offset
   meta[4] : data_arr[]
 */
//=====

//PROC_FS VAR
char procfs_buffer[1024];

static struct proc_dir_entry *ink_proc_file;
static unsigned long procfs_buffer_size = 0;


//========================================
struct breakdown_timestamp {
		 unsigned long long st;
		 unsigned long long et;
		 unsigned long long exec;
};

struct breakdown_param {
		 unsigned long long count;
		 unsigned long long acc;
		 unsigned long long sqr_acc;
};

struct total_param {
		struct breakdown_param sys;
		struct breakdown_param bp;
		struct breakdown_param searching;
		struct breakdown_param tree_modification;
		struct breakdown_param memory_management;
		struct breakdown_param wal;
		struct breakdown_param buffer_transfer;
};

struct global_param {
		struct total_param bd_insert;
		struct total_param bd_update;
		struct total_param bd_search;
};

void init_breakdown_timestamp(struct breakdown_timestamp *bt) {
		bt->st = 0;
		bt->et = 0;
		bt->exec = 0;
}

void init_total_param(struct total_param *tp) {
		tp->sys.count = 0;
		tp->sys.acc = 0;
		tp->sys.sqr_acc = 0;

		tp->bp.count = 0;
		tp->bp.acc = 0;
		tp->bp.sqr_acc = 0;

		tp->searching.count = 0;
		tp->searching.acc = 0;
		tp->searching.sqr_acc = 0;

		tp->tree_modification.count = 0;
		tp->tree_modification.acc = 0;
		tp->tree_modification.sqr_acc = 0;

		tp->memory_management.count = 0;
		tp->memory_management.acc = 0;
		tp->memory_management.sqr_acc = 0;

		tp->wal.count = 0;
		tp->wal.acc = 0;
		tp->wal.sqr_acc = 0;

		tp->buffer_transfer.count = 0;
		tp->buffer_transfer.acc = 0;
		tp->buffer_transfer.sqr_acc = 0;
}

void print_total_param(struct total_param *tp) {

		printk(KERN_INFO "sys- count: %lld acc: %lld sqr_acc: %lld\n", tp->sys.count, tp->sys.acc, tp->sys.sqr_acc);
		printk(KERN_INFO "bp- count: %lld acc: %lld sqr_acc: %lld\n", tp->bp.count, tp->bp.acc, tp->bp.sqr_acc);
		printk(KERN_INFO "searching- count: %lld acc: %lld sqr_acc: %lld\n", tp->searching.count, tp->searching.acc, tp->searching.sqr_acc);
		printk(KERN_INFO "tree_modification- count: %lld acc: %lld sqr_acc: %lld\n", tp->tree_modification.count, tp->tree_modification.acc, tp->tree_modification.sqr_acc);
		printk(KERN_INFO "memory_management- count: %lld acc: %lld sqr_acc: %lld\n", tp->memory_management.count, tp->memory_management.acc, tp->memory_management.sqr_acc);
		printk(KERN_INFO "wal- count: %lld acc: %lld sqr_acc: %lld\n", tp->wal.count, tp->wal.acc, tp->wal.sqr_acc);
		printk(KERN_INFO "buffer transfer - count: %lld acc: %lld sqr_acc: %llc\n", tp->buffer_transfer.count, tp->buffer_transfer.acc, tp->buffer_transfer.sqr_acc);
}

void print_total_stat(struct total_param *tp) {
		unsigned long long sys_avg = 0;
		unsigned long long sys_stddev_sqr = 0;

		unsigned long long searching_avg = 0;
		unsigned long long searching_stddev_sqr = 0;

		unsigned long long bp_avg = 0;
		unsigned long long bp_stddev_sqr = 0;

		unsigned long long wal_avg = 0;
		unsigned long long wal_stddev_sqr = 0;

		unsigned long long tree_avg = 0;
		unsigned long long tree_stddev_sqr = 0;

		unsigned long long memory_avg = 0;
		unsigned long long memory_stddev_sqr = 0;

		unsigned long long buffer_tf_avg = 0;
		unsigned long long buffer_tf_stddev_sqr = 0;

		if(tp->sys.count == 0) {
				//do nothing
		}

		else {
				sys_avg = tp->sys.acc / tp->sys.count;
				sys_stddev_sqr = (tp->sys.sqr_acc - (tp->sys.count * sys_avg * sys_avg)) / tp->sys.count;
				printk(KERN_INFO "sys avg: %lld stdev_sqr: %lld\n", sys_avg, sys_stddev_sqr);

				bp_avg = tp->bp.acc / tp->sys.count;
				bp_stddev_sqr = (tp->bp.sqr_acc - (tp->sys.count * bp_avg * bp_avg)) / tp->sys.count;
				printk(KERN_INFO "bp avg: %lld stdev_sqr: %lld\n", bp_avg, bp_stddev_sqr);

				searching_avg = tp->searching.acc / tp->sys.count;
				searching_stddev_sqr = (tp->searching.sqr_acc - (tp->sys.count * searching_avg * searching_avg)) / tp->sys.count;
				printk(KERN_INFO "searching avg: %lld stdev_sqr: %lld\n", searching_avg, searching_stddev_sqr);

				tree_avg = tp->tree_modification.acc / tp->sys.count;
				tree_stddev_sqr = (tp->tree_modification.sqr_acc - (tp->sys.count * tree_avg * tree_avg)) / tp->sys.count;
				printk(KERN_INFO "tree_modification avg: %lld stdev_sqr: %lld\n", tree_avg, tree_stddev_sqr);

				memory_avg = tp->memory_management.acc / tp->sys.count;
				memory_stddev_sqr = (tp->memory_management.sqr_acc - (tp->sys.count * memory_avg * memory_avg)) / tp->sys.count;
				printk(KERN_INFO "memory_management avg: %lld stdev_sqr: %lld\n", memory_avg, memory_stddev_sqr);

				wal_avg = tp->wal.acc / tp->sys.count;
				wal_stddev_sqr = (tp->wal.sqr_acc - (tp->sys.count * wal_avg * wal_avg)) / tp->sys.count;
				printk(KERN_INFO "wal avg: %lld stdev_sqr: %lld\n", wal_avg, wal_stddev_sqr);

				buffer_tf_avg = tp->buffer_transfer.acc / tp->sys.count;
				buffer_tf_stddev_sqr = (tp->buffer_transfer.sqr_acc - (tp->sys.count * buffer_tf_avg * buffer_tf_avg)) / tp->sys.count;
				printk(KERN_INFO "buffer avg: %lld sddev_sqr: %ld\n", buffer_tf_avg, buffer_tf_stddev_sqr);

				printk(KERN_INFO "avg sum: %lld\n", searching_avg + tree_avg + memory_avg + wal_avg + buffer_tf_avg);
		}
}

static struct global_param global_breakdown_stat;

static spinlock_t bd_lock = __SPIN_LOCK_UNLOCKED();

void update_global_stat(struct total_param *global, struct total_param *instance) {
		//update global total_param with instance total_param
		spin_lock(&bd_lock);

		global->sys.count += instance->sys.count;
		global->sys.acc += instance->sys.acc;
		global->sys.sqr_acc += instance->sys.sqr_acc;

		global->bp.count += instance->bp.count;
		global->bp.acc += instance->bp.acc;
		global->bp.sqr_acc += instance->bp.sqr_acc;

		global->searching.count += instance->searching.count;
		global->searching.acc += instance->searching.acc;
		global->searching.sqr_acc += instance->searching.sqr_acc;

		global->tree_modification.count += instance->tree_modification.count;
		global->tree_modification.acc += instance->tree_modification.acc;
		global->tree_modification.sqr_acc += instance->tree_modification.sqr_acc;

		global->memory_management.count += instance->memory_management.count;
		global->memory_management.acc += instance->memory_management.acc;
		global->memory_management.sqr_acc += instance->memory_management.sqr_acc;

		global->wal.count += instance->wal.count;
		global->wal.acc += instance->wal.acc;
		global->wal.sqr_acc += instance->wal.sqr_acc;

		global->buffer_transfer.count += instance->buffer_transfer.count;
		global->buffer_transfer.acc += instance->buffer_transfer.acc;
		global->buffer_transfer.sqr_acc += instance->buffer_transfer.sqr_acc;

		spin_unlock(&bd_lock);
}


void bd_init(void) {
		init_total_param(&global_breakdown_stat.bd_insert);
		init_total_param(&global_breakdown_stat.bd_update);
		init_total_param(&global_breakdown_stat.bd_search);

		printk(KERN_INFO "bd_insert info\n");
		print_total_param(&global_breakdown_stat.bd_insert);
		printk(KERN_INFO "\n");

		printk(KERN_INFO "bd_update info\n");
		print_total_param(&global_breakdown_stat.bd_update);
		printk(KERN_INFO "\n");

		printk(KERN_INFO "bd_search info\n");
		print_total_param(&global_breakdown_stat.bd_search);
		printk(KERN_INFO "\n");
}

void bd_load(void) {
		printk(KERN_INFO "bd_load() executed\n");

		printk(KERN_INFO "bd_insert info\n");
		print_total_param(&global_breakdown_stat.bd_insert);
		print_total_stat(&global_breakdown_stat.bd_insert);
		printk(KERN_INFO "\n");

		printk(KERN_INFO "bd_update info\n");
		print_total_param(&global_breakdown_stat.bd_update);
		print_total_stat(&global_breakdown_stat.bd_update);
		printk(KERN_INFO "\n");

		printk(KERN_INFO "bd_search info\n");
		print_total_param(&global_breakdown_stat.bd_search);
		print_total_stat(&global_breakdown_stat.bd_search);
		printk(KERN_INFO "\n");

}

static unsigned inline long long mytimestamp(void) {

		//ktime_t mytime;
		unsigned long long mytime = 0;
		mytime = ktime_get_ns();

		return mytime;
}

void timestamp_exec(struct breakdown_timestamp *ts) {
		unsigned long long exec = 0;

		exec = ts->et - ts->st;
		ts->exec = exec;

		if(exec < 0) {
				printk(KERN_INFO "exec is negative: %lld\n", exec);
		}
}

int calc_index (size_t req_size) {
		unsigned int ret = 0;
		//ret = (log(req_size) / log(2));

		if(req_size > 0 && req_size <= 16)
				return 0;
		if(req_size > 16 && req_size <= 32)
				return 1;
		if(req_size > 32 && req_size <= 64)
				return 2;
		if(req_size > 64 && req_size <= 128)
				return 3;
		if(req_size > 128 && req_size <= 256)
				return 4;
		if(req_size > 256 && req_size <= 512)
				return 5;
		if(req_size > 512 && req_size <= 1024)
				return 6;
		if(req_size > 1024 && req_size <= 2048)
				return 7;
		if(req_size > 2048 && req_size <= 4096)
				return 8;
		if(req_size > 4096 && req_size <= 8192)
				return 9;
		if(req_size > 8192 && req_size <= 16384)
				return 10;
		if(req_size > 16384 && req_size <= 32768)
				return 11;
		if(req_size > 32768 && req_size <= 65536)
				return 12;
		if(req_size > 65536 && req_size <= 131072)
				return 13;
		if(req_size > 131072 && req_size <= 262144)
				return 14;
		if(req_size > 262144 && req_size <= 524288)
				return 15;
		if(req_size > 524288 && req_size <= 1048576)
				return 16;
}

size_t calc_size(int index) {
		int i;
		size_t ret = 1;

		for(i = 0; i < index + 4; i++) {
				ret = ret * 2;
		}

		return ret;
}
//========================================

void __clwb(void *addr, size_t data_size, struct total_param *tp) {
	//	struct breakdown_timestamp buffer_timestamp;

		void *start, *end;
		void *start_answer, *end_answer;

	//	init_breakdown_timestamp(&buffer_timestamp);

	//	buffer_timestamp.st = mytimestamp();

		start = (unsigned long long int) addr & 0xFFFFFFFFFFFFFFC0;
		end = ((unsigned long long int) addr + data_size + 63UL) & 0xFFFFFFFFFFFFFFC0;

		asm volatile("mfence":::"memory");
		while(start <= end) {
				clwb(start);
				start = start + 64UL;
		}
		asm volatile("mfence":::"memory");

//		buffer_timestamp.et = mytimestamp();
//		timestamp_exec(&buffer_timestamp);
//		tp->buffer_transfer.count++;
//		tp->buffer_transfer.acc += buffer_timestamp.exec;
//		tp->buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);
}

void backup_process(void *target_offset, size_t target_size, struct total_param *tp) {

		//-=====
//		unsigned long long dup_st, dup_et, dup_exec;
//		struct breakdown_timestamp wal_timestamp;
//		init_breakdown_timestamp(&wal_timestamp);
//		dup_exec = 0;
		//=====

		size_t first_leng, second_leng;
		void *target_addr = (void *)target_offset + (size_t)pmem;

		void *jour_tmp = jour_curr;

		//=====
		//wal_timestamp.st = mytimestamp();
		//-=====

		down_write(&backup_rwsem);

		if(target_offset == NULL) {
				//do nothing
		}

		else {
				sprintf(jour_tmp, "0x%012llx;%08ld;", (long long unsigned int)target_offset, target_size);
				first_leng = strlen(jour_tmp);
				jour_tmp = jour_tmp + first_leng;

				second_leng = target_size;
				memcpy(jour_tmp, target_addr, second_leng);
				jour_tmp = jour_tmp + second_leng;

				strcpy(jour_tmp, ";");
				jour_tmp = jour_tmp + 1;

				jour_curr = jour_tmp;

				meta[3] = ((void *)jour_curr - pmem);
				//=======
				//dup_st = mytimestamp();
				__clwb(target_addr, first_leng+second_leng+target_size+3, tp);
				__clwb(&meta[3], sizeof(void *), tp);
				//dup_et = mytimestamp();
				//dup_exec = dup_et - dup_st;
				//=======
		}
			//=====
		up_write(&backup_rwsem);


//		wal_timestamp.et = mytimestamp();
//		timestamp_exec(&wal_timestamp);
//		wal_timestamp.exec -= dup_exec;
//
//		tp->wal.count++;
//		tp->wal.acc += wal_timestamp.exec;
//		tp->wal.sqr_acc += (wal_timestamp.exec * wal_timestamp.exec);
		//-=-===
}

void recovery_process(void) {

		if(jour_addr == jour_curr) {
				printk(KERN_INFO "no need to recovery\n");
		}

		else {
				void *backup_data = kzalloc(sizeof(char) * (jour_curr - jour_addr), GFP_KERNEL);
				void *backup_curr = backup_data;
				
				size_t *offset = kzalloc(sizeof(void *) * 1024, GFP_KERNEL);
				size_t *length = kzalloc(sizeof(void *) * 1024, GFP_KERNEL);
				void **target = kzalloc(sizeof(void *) * 1024 * 1024, GFP_KERNEL);
				int i = 0; int x = 0;

				//copy backup data in NVMe to main memory
				memcpy(backup_data, jour_addr, sizeof(char) * (jour_curr - jour_addr));

				//Ordering
				while((backup_curr-backup_data) <= (jour_curr-jour_addr)) {

						int ret;
						char *offset_data = kzalloc(sizeof(char) * 14, GFP_KERNEL);
						char *length_data = kzalloc(sizeof(char) * 8, GFP_KERNEL);

						strncpy(offset_data, backup_curr, sizeof(char) * 14);
						backup_curr += 15;
						//offset[i] = kstrtol(offset_data, NULL, 16);
						ret = kstrtol(offset_data, 16, &offset[i]);

						strncpy(length_data, backup_curr, sizeof(char)*8);
						backup_curr += 9;
						//length[i] = kstrtol(length_data, NULL, 10);
						ret = kstrtol(length_data, 16, &length[i]);

						//memcpy(target, backup_curr, sizeof(char) * length);
						target[i] = backup_curr;
						backup_curr = (void *)backup_curr + (size_t)(length[i] + 1);

						kfree(offset_data);
						kfree(length_data);
						i++;
				}

				i = i - 2;
				for(x = i; x >= 0; x--) {
						void *target_addr;
						target_addr = (void *)offset[x] + (size_t)pmem;
						memcpy(target_addr, target[x], length[x]);
				}

				kfree(backup_data);
				kfree(offset);
				kfree(length);
				kfree(target);

		}
}


void init_recov(void) {

		if(jour_curr != jour_addr) {
				down_write(&backup_rwsem);
				//bp_down_write(&backup_rwsem);

				memset(jour_addr, 0, jour_curr - jour_addr);
		jour_curr = jour_addr;

//		meta[3] = jour_curr - pmem;
		meta[3] = ((void *)jour_curr - pmem);

		up_write(&backup_rwsem);
		//bp_up_write(&backup_rwsem);
		}

		else {
				//do nothing
		}
}

void changeOffset(void *dest, void *src, size_t size) {

		if(size != 0) {
				void *mem_node = kmalloc(sizeof(void *) * (size), GFP_KERNEL);
				memset(mem_node, 0, (sizeof(void *) * (size)));

				memcpy(mem_node, src, size);
				memset(dest, 0, size);
				memcpy(dest, mem_node, size);

				kfree(mem_node);
		}
}

void clear_bp_node(struct bp_node *input_node) {
		int i = 0;
		struct bp_node *node = (void *)input_node + (size_t)pmem;

		memset(node, 0, offsetof(struct bp_node, node_rwsem));
}

void free_bp_node(struct bp_node *input_node, struct total_param *tp) {

		if(input_node == NULL) {
				//do nothing
		}
		else {
				//int index = sizeof(struct bp_node) / 16;
				int index = 0;
				void *list_offset;
				struct free_node *fn, *tmp_fn;

				down_write(&list_rwsem);
				//bp_down_write(&list_rwsem);

				index = calc_index(sizeof(struct bp_node));
				clear_bp_node(input_node);

				//backup need
				list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
				backup_process(data_arr[index].head, sizeof(struct free_node), tp);
				backup_process(data_arr[index].tail, sizeof(struct free_node), tp);
				backup_process(list_offset, sizeof(struct free_list), tp);

				fn = (void *)input_node;
				tmp_fn = (void *)fn + (size_t)pmem;
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

				__clwb(&data_arr[index], sizeof(struct free_list), tp);
		}

		up_write(&list_rwsem);
		//bp_up_write(&list_rwsem);
}

struct bp_node *alloc_bp_node(struct total_param *tp) {
		struct bp_node *ret = NULL;

		//int index = sizeof(struct bp_node) / 16;
		int index = 0;
		void *list_offset;

//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//		struct breakdown_timestamp memory_timestamp;
//		init_breakdown_timestamp(&memory_timestamp);
//		dup_total = 0;

//		memory_timestamp.st = mytimestamp();

		down_write(&list_rwsem);
		//bp_down_write(&list_rwsem);
		index = calc_index(sizeof(struct bp_node));

		//backup need
		list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
//		dup_st = mytimestamp();
		backup_process(data_arr[index].head, sizeof(struct free_node), tp);
		backup_process(data_arr[index].tail, sizeof(struct free_node), tp);
		backup_process(list_offset, sizeof(struct free_list), tp);
//		dup_et = mytimestamp();

//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		if(data_arr[index].nr_node > 0) {
				struct free_node *fn = (void *)data_arr[index].head + (size_t)pmem;
				data_arr[index].head = fn->next;
				ret = (void *)fn;
				data_arr[index].nr_node--;
		}
		else if(data_arr[index].nr_node == 0) {
				ret = next_ptr;
				//meta[1] = meta[1] + ((index + 1) * 16);
				//next_ptr = next_ptr + ((index + 1) * 16);
				meta[1] = meta[1] + (calc_size(index));
				next_ptr = next_ptr + (calc_size(index));
		}
		alloc_count[index]++;

//		dup_st = mytimestamp();
		__clwb(&data_arr[index], sizeof(struct free_list), tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		up_write(&list_rwsem);

//		memory_timestamp.et = mytimestamp();
//		timestamp_exec(&memory_timestamp);
//		memory_timestamp.exec -= dup_total;
//
//		tp->memory_management.count++;
//		tp->memory_management.acc += memory_timestamp.exec;
//		tp->memory_management.sqr_acc += (memory_timestamp.exec * memory_timestamp.exec);

		return ret;
}

void free_string(char *input_string, struct total_param *tp) {
		//=====
//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//
//		struct breakdown_timestamp memory_timestamp;
//		init_breakdown_timestamp(&memory_timestamp);
//		dup_total = 0;
//
//		memory_timestamp.st = mytimestamp();
		//=====

		if(input_string == NULL) {
				//do nothing
		}
		else {

				char *string = NULL;
				int index = 0;

				struct free_node *fn = NULL;
				struct free_node *tmp_fn = NULL;
				void *list_offset;

				down_write(&list_rwsem);
				//bp_down_write(&list_rwsem);

				string = (void *)input_string + (size_t)pmem;
				//index = strlen(string)/16;
				index = calc_index(strlen(string));

				fn = (void *)input_string;
				tmp_fn = (void *)fn + (size_t)pmem;
				tmp_fn->next = NULL;

				//recovery needed
				list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
			//	dup_st = mytimestamp();
				backup_process(data_arr[index].head, sizeof(struct free_node), tp);
				backup_process(data_arr[index].tail, sizeof(struct free_node), tp);
				backup_process(list_offset, sizeof(struct free_list), tp);
//				dup_et = mytimestamp();
//
//				dup_exec = dup_et - dup_st;
//				dup_total += dup_exec;
			
				if(data_arr[index].nr_node == 0) {
						data_arr[index].head = (void *)fn;
						data_arr[index].tail = (void *)fn;
						data_arr[index].nr_node++;
				}
				else if(data_arr[index].nr_node > 0) {
						struct free_node *tail_fn = NULL;

						tail_fn = (void *)data_arr[index].tail + (size_t)pmem;
						tail_fn -> next = (void *)fn;
						data_arr[index].tail = (void *)fn;
						data_arr[index].nr_node++;
				}

//				dup_st = mytimestamp();
				__clwb(&data_arr[index], sizeof(struct free_list), tp);
//				dup_et = mytimestamp();
//				dup_exec = dup_et - dup_st;
//				dup_total += dup_exec;
		}
		up_write(&list_rwsem);
		//bp_up_write(&list_rwsem);

		//=-====
//		memory_timestamp.et = mytimestamp();
//		timestamp_exec(&memory_timestamp);
//		memory_timestamp.exec -= dup_total;
//
//		tp->memory_management.count++;
//		tp->memory_management.acc += memory_timestamp.exec;
//		tp->memory_management.sqr_acc += (memory_timestamp.exec * memory_timestamp.exec);
		//=====
}

char *alloc_string(size_t length, struct total_param *tp) {

		//=====
//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//		struct breakdown_timestamp memory_timestamp;
//		init_breakdown_timestamp(&memory_timestamp);
//		dup_total = 0;
		//=====

		char *ret = NULL;
		int index = 0;

		void *list_offset;

		//=====
//		memory_timestamp.st = mytimestamp();
		//=====

		//index = length/16;
		index = calc_index(length);

		down_write(&list_rwsem);
		//bp_down_write(&list_rwsem);

		//recovery needed
		list_offset = (struct free_list *)((void *)&data_arr[index] - pmem);
//		dup_st = mytimestamp();
		backup_process(data_arr[index].head, sizeof(struct free_list), tp);
		backup_process(data_arr[index].tail, sizeof(struct free_list), tp);
		backup_process(list_offset, sizeof(struct free_list), tp);
//		dup_et = mytimestamp();
//
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;
		//=======================

		if(data_arr[index].nr_node > 0) {
				struct free_node *fn = NULL;

				fn= (void *)data_arr[index].head + (size_t)pmem;
				data_arr[index].head = fn->next;
				ret = (void *)fn;
				data_arr[index].nr_node--;
		}
		else if(data_arr[index].nr_node == 0) {
				ret = next_ptr;
				//meta[1] = meta[1] + ((index + 1) * 16);
				meta[1] = meta[1] + (calc_size(index));
				//next_ptr = next_ptr + ((index + 1) * 16);
				next_ptr = next_ptr + (calc_size(index));

		}
		alloc_count[index]++;

//		dup_st = mytimestamp();
		__clwb(&data_arr[index], sizeof(struct free_list), tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		//clear obj
		//memset(ret, 0, (index+1)*16);
		memset(ret, 0, (calc_size(index)));
		up_write(&list_rwsem);
		//bp_up_write(&list_rwsem);

		//-=----
//		memory_timestamp.et = mytimestamp();
//		timestamp_exec(&memory_timestamp);
//		memory_timestamp.exec -= dup_total;
//
//		tp->memory_management.count++;
//		tp->memory_management.acc += memory_timestamp.exec;
//		tp->memory_management.sqr_acc += (memory_timestamp.exec * memory_timestamp.exec);
		//=====

		return ret;
}

struct bp_node *bp_node_create(struct total_param *tp) {

		struct bp_node *new_node = alloc_bp_node(tp);

		memset(new_node, 0, sizeof(struct bp_node));
		init_rwsem(&new_node->node_rwsem);

		new_node = (struct bp_node *)((void *)new_node - pmem);
		return new_node;
}


void removeValue(struct bp_node *input_node, int index, struct total_param *tp) {
		//=====
//		struct breakdown_timestamp tree_ts;
//		init_breakdown_timestamp(&tree_ts);
		//=====

		struct bp_node *node = (void *)input_node + (size_t)pmem; 

		//backup need
		char *backup_key, *backup_value;

		//-----
//		tree_ts.st = mytimestamp();
		//-----

		backup_key = (void *)node->index[index] + (size_t)pmem;
		backup_value = (void *)node->children[index] + (size_t)pmem;

		//===
		backup_process(node->index[index], strlen(backup_key), tp);
		backup_process(node->children[index], strlen(backup_value), tp);
		backup_process(input_node, sizeof(struct bp_node), tp);

		//free(node->index[index]);
		free_string(node->index[index], tp);
		node->index[index] = NULL;
		//free(node->children[index]);
		free_string(node->children[index], tp);
		node->children[index] = NULL;

		if(index <= node->nr_chunks - 1) {
				changeOffset(&node->index[index], &node->index[index+1], sizeof(void *) * (node->nr_chunks - index - 1));
				changeOffset(&node->children[index], &node->children[index+1], sizeof(struct BTreeNode *) * (node->nr_chunks - index));
		}
		node->nr_chunks--;


		//=====
//		tree_ts.et = mytimestamp();
//		timestamp_exec(&tree_ts);
//		tp->tree_modification.count++;
//		tp->tree_modification.acc += tree_ts.exec;
//		tp->tree_modification.sqr_acc += (tree_ts.exec * tree_ts.exec);
		//=====

}

//return index in node
int findValue(struct bp_node *input_node, char *input_key) {
		//=====
//		long st = 0;
//		long et = 0;
//		long exec = 0;
		//=====

		struct bp_node *node = (void *)input_node + (size_t)pmem;
		char *key              = input_key          + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

//		st = mytimestamp();

		while (low <= high) {
				int mid = (low + high) / 2;
				char *mid_key;

				mid_key = (void *)node->index[mid] + (size_t)pmem;

				if (strcmp(key, mid_key) == 0)
						return mid;
				else {
						if (strcmp(key, mid_key) > 0)
								low = mid + 1;
						else
								high = mid - 1;
				}
		}

//		et = mytimestamp();
//		exec = et - st;

		//bd_arr[4].count++;
		//bd_arr[4].acc += exec;
		//bd_arr[4].sqr_acc += (exec * exec);

		return -1;
}

int findValue_search(struct bp_node *input_node, char *key, struct total_param *tp) {
		//=====
//		struct breakdown_timestamp searching_timestamp;
//		init_breakdown_timestamp(&searching_timestamp);

		//=====


		struct bp_node *node = (void *)input_node + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		//=====
//		searching_timestamp.st = mytimestamp();
		//=====

		while (low <= high) {
				int mid = (low + high) / 2;
				char *mid_key;

				mid_key = (void *)node->index[mid] + (size_t)pmem;

				if (strcmp(key, mid_key) == 0){
						//=====
//						searching_timestamp.et = mytimestamp();
//						timestamp_exec(&searching_timestamp);
//						tp->searching.count++;
//						tp->searching.acc += searching_timestamp.exec;
//						tp->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
						//=====

						return mid;
				}
				else {
						if (strcmp(key, mid_key) > 0)
								low = mid + 1;
						else
								high = mid - 1;
				}
		}

		//=====
//		searching_timestamp.et = mytimestamp();
//		timestamp_exec(&searching_timestamp);
//		tp->searching.count++;
//		tp->searching.acc += searching_timestamp.exec;
//		tp->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
		//=====

		return -1;
}

int nextNodeIndex(struct bp_node *input_node, char *input_key, struct total_param *tp) {
		//=====
//		struct breakdown_timestamp search_ts;
//		init_breakdown_timestamp(&search_ts);
		//=====


		struct bp_node *node = (void *)input_node + (size_t)pmem;
		char *key              = input_key + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		//=====
//		search_ts.st = mytimestamp();
		//=====

		while(low <= high) {
				int mid = (low + high) / 2;

				char *mid_key = (void *)node->index[mid] + (size_t)pmem;

				if (strcmp(key, mid_key) > 0)
						low = mid + 1;
				else
						high = mid - 1;
		}

		if (low == high) {
				//=====
//				search_ts.et = mytimestamp();
//				timestamp_exec(&search_ts);
//				tp->searching.count++;
//				tp->searching.acc += search_ts.exec;
//				tp->searching.sqr_acc += (search_ts.exec * search_ts.exec);
				//=====

				return low+1;
		}
		else {
				//=====
//				search_ts.et = mytimestamp();
//				timestamp_exec(&search_ts);
//				tp->searching.count++;
//				tp->searching.acc += search_ts.exec;
//				tp->searching.sqr_acc += (search_ts.exec * search_ts.exec);
				//=====

				return low;
		}

}

int nextNodeIndex_search(struct bp_node *input_node, char *key, struct total_param *tp) {
		//=====
//		struct breakdown_timestamp searching_timestamp;
//		init_breakdown_timestamp(&searching_timestamp);
		//=====


		struct bp_node *node = (void *)input_node + (size_t)pmem;

		int low = 0;
		int high = node->nr_chunks - 1;

		//=====
//		searching_timestamp.st = mytimestamp();
		//=====


		while(low <=high) {
				int mid = (low + high) / 2;

				char *mid_key = (void *)node->index[mid] + (size_t)pmem;

				if(strcmp(key, mid_key) > 0)
						low = mid + 1;
				else
						high = mid - 1;
		}

		if( low == high) {
				//=====
//				searching_timestamp.et = mytimestamp();
//				timestamp_exec(&searching_timestamp);
//				tp->searching.count++;
//				tp->searching.acc += searching_timestamp.exec;
//				tp->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
				//=-====

				return low + 1;
		}

		else {
				//=====
//				searching_timestamp.et = mytimestamp();
//				timestamp_exec(&searching_timestamp);
//				tp->searching.count++;
//				tp->searching.acc += searching_timestamp.exec;
//				tp->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
				//=-====

				return low;
		}
}
//basic operations.
//left, right for split

void addValue(struct bp_node *input_node, char *input_key, void *input_left, void *input_right, struct total_param *tp) {
		//=====
		int i;
		struct bp_node *node, *left, *right;
//		unsigned long long dup_st, dup_et, dup_total, dup_exec;
//		struct breakdown_timestamp tree_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//		init_breakdown_timestamp(&tree_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//		dup_exec = 0;
//		dup_total = 0;
		//=====

		//=====
//		tree_timestamp.st = mytimestamp();
		//=====

		node = (void *)input_node + (size_t)pmem; left = (void *)input_left + (size_t)pmem;
		right = (void *)input_right + (size_t)pmem;

//		dup_st = mytimestamp();
		i = nextNodeIndex(input_node, input_key, tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		tp->searching.count--;
//		tp->searching.acc -= dup_exec;
//		tp->searching.sqr_acc -= (dup_exec * dup_exec);

		//recovery_need
		//except from tree_modification part
//		dup_exec = 0;
//		dup_st = mytimestamp();
		backup_process(input_node, sizeof(struct bp_node), tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//
		node->index[node->nr_chunks] = NULL;
		node->children[node->nr_chunks + 1] = NULL;


		if(node->is_leaf == FALSE) {
				//do non-leaf operation
				changeOffset(&node->index[i+1], &node->index[i], sizeof(void *) * (node->nr_chunks - i));
				changeOffset(&node->children[i+2], &node->children[i+1], sizeof(void *) * (node->nr_chunks - i));

				node->nr_chunks++;
				node->index[i] = input_key;
				node->children[i] = input_left;
				node->children[i+1] = input_right;
		}

		else {
				changeOffset(&node->index[i+1], &node->index[i], sizeof(void *) * (node->nr_chunks - i));
				changeOffset(&node->children[i+1], &node->children[i], sizeof(void *) * (node->nr_chunks - i));

				node->nr_chunks++;
				node->index[i] = input_key;
				node->children[i] = input_left;
				//node->children[i+1] = right;
		}

		__clwb(node, sizeof(struct bp_node), tp);

		//=====
//		tree_timestamp.et = mytimestamp();
//		timestamp_exec(&tree_timestamp);
//		tree_timestamp.exec -= dup_exec;
//
//		tp->tree_modification.count++;
//		tp->tree_modification.acc += tree_timestamp.exec;
//		tp->tree_modification.sqr_acc += (tree_timestamp.exec * tree_timestamp.exec);
		//=====
}


struct bp_node *bp_node_splitRoot(struct bp_node *input_parentNode, struct total_param *tp) {
		//=====
		struct bp_node *parentNode;
		struct bp_node *left, *right, *split;
		char *cp, *temp;

//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//		struct breakdown_timestamp tree_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//		init_breakdown_timestamp(&tree_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//		dup_total = 0;
//		dup_exec = 0;
		//=====

		//=====
//		tree_timestamp.st = mytimestamp();
		//=====

		parentNode = (void *)input_parentNode + (size_t)pmem;
		split = parentNode;

//		dup_st = mytimestamp();
		backup_process(input_parentNode, sizeof(struct bp_node), tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		
//		dup_st = mytimestamp();
		left = bp_node_create(tp);
		left = (void *)left + (size_t)pmem;

		right = bp_node_create(tp);
		right = (void *)right + (size_t)pmem;
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;
	
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
		}

		//char *temp = malloc(sizeof(char) * strlen(split->index[INDEX_MAX/2]));
		//strcpy(temp, split->index[INDEX_MAX/2]);
		cp = (void *)split->index[INDEX_MAX/2] + (size_t)pmem;


//		dup_st = mytimestamp();
		temp = alloc_string(strlen(cp), tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		//strcpy(temp, cp);
		memcpy(temp, cp, strlen(cp));

		__clwb(left, sizeof(struct bp_node), tp);
		__clwb(right, sizeof(struct bp_node), tp);

		left = (void *)((void *)left - pmem);
		right = (void *)((void *)right - pmem);
		split = (void *)((void *)split - pmem);
		temp = (void *)((void *)temp - pmem);
		clear_bp_node(split);

//		dup_st = mytimestamp();
		addValue(split, temp, left, right, tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;



		//split = (void *)split + (size_t)pmem;


		//=====
//		tree_timestamp.et = mytimestamp();
//		timestamp_exec(&tree_timestamp);
//		tree_timestamp.exec -= dup_total;
//
//		tp->tree_modification.count++;
//		tp->tree_modification.acc += tree_timestamp.exec;
//		tp->tree_modification.sqr_acc += (tree_timestamp.exec * tree_timestamp.exec);
		//-----

		return input_parentNode;
}

struct bp_node *bp_node_splitNormal(struct bp_node *input_currentNode, struct bp_node *input_parentNode, char *input_key, struct total_param *tp) {
		//=====
//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//		struct breakdown_timestamp tree_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//		init_breakdown_timestamp(&tree_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//		dup_total = 0;
//		dup_exec = 0;
		//=====

		struct bp_node *currentNode = (void *)input_currentNode + (size_t)pmem;
		struct bp_node *parentNode = (void *)input_parentNode + (size_t)pmem;

		struct bp_node *left, *right;
		struct bp_node *split, *offset_split;

		char *cp, *temp;

		int i;

		//=====
//		tree_timestamp.st = mytimestamp();
		//=====

		split = parentNode;

		//change to nextNodeIndex_search
		//is this really need? anyway answer always be ARRAY_NODE/2 because its algorithm...?
//		dup_st = mytimestamp();
		i = nextNodeIndex_search(input_parentNode, input_key, tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		tp->searching.count--;
//		tp->searching.acc -= dup_exec;
//		tp->searching.sqr_acc -= dup_exec;

		//backup need
//		dup_st = mytimestamp;
		backup_process(input_parentNode, sizeof(struct bp_node), tp);

//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;
//
		if(currentNode->is_leaf == TRUE) {
				//backup_need
				char *backup_value = (void *)parentNode->children[i] + (size_t)pmem;

//				dup_st = mytimestamp();
				backup_process(parentNode->children[i], sizeof(strlen(backup_value)), tp);
//				dup_et = mytimestamp();
//				dup_exec = dup_et - dup_st;
//				dup_total += dup_exec;

				//do leaf operation
				left = parentNode->children[i];
				left = (void *)left + (size_t)pmem;
				left->nr_chunks = (INDEX_MAX / 2) + 1;
				left->is_leaf = TRUE;

				right = bp_node_create(tp);
				right = (void *)right + (size_t)pmem;
				right->nr_chunks = INDEX_MAX / 2;
				right->is_leaf = TRUE;

				changeOffset(&right->index[0], &left->index[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1)));
				changeOffset(&right->children[0], &left->children[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1) + 1));
		}

		else {
				//backup need
				//dup_st = mytimestamp();
				backup_process(parentNode->children[i], sizeof(struct bp_node), tp);
//				dup_et = mytimestamp();
//				dup_exec = dup_et - dup_st;
//				dup_total += dup_exec;

				//do non-leaf(internal node) operation
				left = parentNode->children[i];
				left = (void *)left + (size_t)pmem;
				left->nr_chunks = INDEX_MAX / 2;
				left->is_leaf = FALSE;

				right = bp_node_create(tp);
				right = (void *)right + (size_t)pmem;
				right->nr_chunks = INDEX_MAX / 2;
				right->is_leaf = FALSE;

				changeOffset(&right->index[0], &left->index[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1)));
				changeOffset(&right->children[0], &left->children[(INDEX_MAX/2)+1], sizeof(void *) * (INDEX_MAX - ((INDEX_MAX/2)+1) + 1));
		}


		cp = (void *)left->index[INDEX_MAX/2] + (size_t)pmem;

//		dup_st = mytimestamp();
		temp = alloc_string(strlen(cp), tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		//strcpy(temp, cp);
		memcpy(temp, cp, strlen(cp));

		__clwb(left, sizeof(struct bp_node), tp);
		__clwb(right, sizeof(struct bp_node), tp);

		//		
		left = (void *)((void *)left - pmem);
		right = (void *)((void *)right - pmem);
		temp = (void *)((void *)temp - pmem);
//		input_parentNode = (struct bp_node *)((void *)parentNode - pmem);

//		dup_st = mytimestamp();
		addValue(input_parentNode, temp, left, right, tp);
//		dup_et = mytimestamp();
//		dup_exec = dup_et - dup_st;
//		dup_total += dup_exec;

		offset_split = (void *)((void *)split - pmem);

		//=====
//		tree_timestamp.et = mytimestamp();
//		timestamp_exec(&tree_timestamp);
//		tp->tree_modification.count++;
//		tp->tree_modification.acc += tree_timestamp.exec;
//		tp->tree_modification.sqr_acc += (tree_timestamp.exec * tree_timestamp.exec);
		//=====

		return offset_split;
}

int bp_insert(char *input_key, size_t key_len, char *input_value, size_t value_len, struct total_param *insert_breakdown) {
		struct bp_node *currentNode, *parentNode, *input_node;

		//for allocation
		char *allocated_key = NULL;
		char *allocated_value = NULL;

		//======
//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//		struct breakdown_timestamp buffer_timestamp;
//		struct breakdown_timestamp searching_timestamp;
//		struct breakdown_timestamp tree_timestamp;
//		init_breakdown_timestamp(&tree_timestamp);
//		init_breakdown_timestamp(&searching_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//		dup_total = 0;
		//=====

//		searching_timestamp.st = mytimestamp();

		parentNode = root;
		currentNode = parentNode;
		currentNode = (struct bp_node *)((void *)currentNode - pmem);
//		searching_timestamp.et = mytimestamp();
//		timestamp_exec(&searching_timestamp);
//		insert_breakdown->searching.acc += searching_timestamp.exec;
//		insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
//		insert_breakdown->searching.count++;

		while(TRUE) {
				struct bp_node *input_currentNode;
				struct bp_node *input_parentNode;
				struct bp_node *next_node;
				struct bp_node *origin_splitNormal;
				int is_splitNormal = FALSE;
				int ret_findValue, next;

//				searching_timestamp.st = mytimestamp();
				currentNode = (void *)currentNode + (size_t)pmem;
				input_currentNode = (struct bp_node *)((void *)currentNode - pmem);
				input_parentNode = (struct bp_node *)((void *)parentNode - pmem);

				//GET_LOCK
				if(currentNode == root) {
						down_write(&currentNode->node_rwsem);
						//bp_down_write(&currentNode->node_rwsem);
				}
				else {
						down_write(&parentNode->node_rwsem);
						down_write(&currentNode->node_rwsem);
						//bp_down_write(&parentNode->node_rwsem);
						//bp_down_write(&currentNode->node_rwsem);
				}

//				searching_timestamp.et = mytimestamp();
//				timestamp_exec(&searching_timestamp);
//				insert_breakdown->searching.acc += searching_timestamp.exec;
//				insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
//				insert_breakdown->searching.count++;

				ret_findValue = findValue_search(input_currentNode, input_key, insert_breakdown);

				if(ret_findValue >= 0 && currentNode->is_leaf == TRUE) {
						//update!
						//이걸 계산해봐서 크기가 같은 chunk에 들어가면 overwrite하는 걸로다가 바꿀것!
						char *legacy_value; 
						int legacy_index;
						int input_index;

						legacy_value = (void *)currentNode->children[ret_findValue] + (size_t)pmem;
						//원래 들어있던 value값이 뭐였는지 백업하는 부분.
						backup_process(currentNode->children[ret_findValue], strlen(legacy_value), insert_breakdown);
						//backup_process(legacy_value, strlen(legacy_value), insert_breakdown);

						//if 계산결과 input_value chunk 사이즈가 legacy_value chunk사이즈와 같다면
						//걍 오버라이트
//						searching_timestamp.st = mytimestamp();
						//legacy_index = strlen(legacy_value) / 16;
						legacy_index = calc_index(strlen(legacy_value));
						//input_index = value_len / 16;
						input_index = calc_index(value_len);
//						searching_timestamp.et = mytimestamp();
//						timestamp_exec(&searching_timestamp);
//						insert_breakdown->searching.acc += searching_timestamp.exec;
//						insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
//						insert_breakdown->searching.count++;

						if(legacy_index == input_index) {
								//overwrite to same chunk
					//			buffer_timestamp.st = mytimestamp();
								//strcpy(legacy_value, input_value);
								copy_from_user(legacy_value, input_value, value_len);
								__clwb(legacy_value, strlen(legacy_value), insert_breakdown);
//								buffer_timestamp.et= mytimestamp();
//								timestamp_exec(&buffer_timestamp);
//								insert_breakdown->buffer_transfer.count++;
//								insert_breakdown->buffer_transfer.acc += buffer_timestamp.exec;
//								insert_breakdown->buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);

								}


						else {
								//else시 밑에 부분.
								//여기가 비어있던 allocated_value에 DCPM 할당해 주는 파트
								//allocated_value = alloc_string(strlen(input_value), insert_breakdown);
								allocated_value = alloc_string(value_len, insert_breakdown);
								
							//	buffer_timestamp.st = mytimestamp();

								//strcpy(allocated_value, input_value);
								copy_from_user(allocated_value, input_value, value_len);
								//__clwb(allocated_value, strlen(input_value), insert_breakdown);
								__clwb(allocated_value, value_len, insert_breakdown);

//								buffer_timestamp.et= mytimestamp();
//								timestamp_exec(&buffer_timestamp);
//								insert_breakdown->buffer_transfer.count++;
//								insert_breakdown->buffer_transfer.acc += buffer_timestamp.exec;
//								insert_breakdown->buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);
//
//								tree_timestamp.st = mytimestamp();
								
								allocated_value = (char *)((void *)allocated_value - pmem);

								//원래 있던 값 free하고 넣는 것.
//								dup_exec = 0;
//								dup_st = mytimestamp();
								free_string(currentNode->children[ret_findValue], insert_breakdown);
//								dup_et = mytimestamp();
//								dup_exec = dup_et - dup_st;

								currentNode->children[ret_findValue] = allocated_value;
								
								__clwb(currentNode, sizeof(struct bp_node), insert_breakdown);
//								tree_timestamp.et = mytimestamp();
//								timestamp_exec(&tree_timestamp);
//								tree_timestamp.exec -= dup_exec;
//								insert_breakdown->tree_modification.count++;
//								insert_breakdown->tree_modification.acc += tree_timestamp.exec;
//								insert_breakdown->tree_modification.sqr_acc += tree_timestamp.exec;;
						}


	//					searching_timestamp.st = mytimestamp();
						//PUT LOCK
						if(currentNode == root) {
								up_write(&currentNode->node_rwsem);
								//bp_up_write(&currentNode->node_rwsem);
						}
						else {
								up_write(&currentNode->node_rwsem);
								up_write(&parentNode->node_rwsem);
								//bp_up_write(&currentNode->node_rwsem);
								//bp_up_write(&parentNode->node_rwsem);
						}

//						searching_timestamp.et = mytimestamp();
//						timestamp_exec(&searching_timestamp);
//						searching_timestamp.exec -= dup_total;
//						insert_breakdown->searching.acc += searching_timestamp.exec;
//						insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
//						insert_breakdown->searching.count++;

						return 1;
				}

				if(currentNode->nr_chunks == INDEX_MAX) {
						if(currentNode == root) {
								currentNode = bp_node_splitRoot(input_parentNode, insert_breakdown);

//								searching_timestamp.st = mytimestamp();
								currentNode = (void *)currentNode + (size_t)pmem;
//								searching_timestamp.et = mytimestamp();
//								timestamp_exec(&searching_timestamp);
//								insert_breakdown->searching.acc += searching_timestamp.exec;
//								insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
//								insert_breakdown->searching.count++;
						}
						else {
			//					searching_timestamp.st = mytimestamp();
								origin_splitNormal = currentNode;
								is_splitNormal = TRUE;
//								searching_timestamp.et = mytimestamp();
//								timestamp_exec(&searching_timestamp);
//								insert_breakdown->searching.count++;
//								insert_breakdown->searching.acc += searching_timestamp.exec;
//								insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);

								currentNode = bp_node_splitNormal(input_currentNode, input_parentNode, input_key, insert_breakdown);

//								searching_timestamp.st = mytimestamp();
								currentNode = (void *)currentNode + (size_t)pmem;
//								searching_timestamp.et = mytimestamp();
//								timestamp_exec(&searching_timestamp);
//								insert_breakdown->searching.count++;
//								insert_breakdown->searching.acc += searching_timestamp.exec;
//								insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
						}
				}


//				searching_timestamp.st = mytimestamp();
				if(currentNode->is_leaf == TRUE) {
						break;
				}

				input_currentNode = (struct bp_node *)((void *)currentNode - pmem);
//				searching_timestamp.et = mytimestamp();
//				timestamp_exec(&searching_timestamp);
//				insert_breakdown->searching.count++;
//				insert_breakdown->searching.acc += searching_timestamp.exec;
//				insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);

				next = nextNodeIndex_search(input_currentNode, input_key, insert_breakdown);

//				searching_timestamp.st = mytimestamp();
				if(currentNode == root && is_splitNormal == FALSE) {
						up_write(&currentNode->node_rwsem);
						//bp_up_write(&currentNode->node_rwsem);
				}
				else if (is_splitNormal == TRUE) {
						is_splitNormal = FALSE;
						up_write(&origin_splitNormal->node_rwsem);
					//bp_up_write(&origin_splitNormal->node_rwsem);
						up_write(&currentNode->node_rwsem);
						//bp_up_write(&currentNode->node_rwsem);
				}
				else {
						up_write(&currentNode->node_rwsem);
						//bp_up_write(&currentNode->node_rwsem);
						up_write(&parentNode->node_rwsem);
						//bp_up_write(&parentNode->node_rwsem);
				}

				parentNode = currentNode;
				currentNode = currentNode->children[next];
//				searching_timestamp.et = mytimestamp();
//				timestamp_exec(&searching_timestamp);
//				insert_breakdown->searching.count++;
//				insert_breakdown->searching.acc += searching_timestamp.exec;
//				insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
		}

//		searching_timestamp.st = mytimestamp();
		input_node = (struct bp_node *)((void *)currentNode - pmem);
//		searching_timestamp.et = mytimestamp();
//		timestamp_exec(&searching_timestamp);
//		insert_breakdown->searching.count++;
//		insert_breakdown->searching.acc += searching_timestamp.exec;
//		insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);

		allocated_key = alloc_string(key_len, insert_breakdown);
		//allocated_value = alloc_string(strlen(input_value), insert_breakdown);
		allocated_value = alloc_string(value_len, insert_breakdown);
		//strcpy instead memcpy
//		buffer_timestamp.st = mytimestamp();
		//strcpy(allocated_key, input_key);
		memcpy(allocated_key, input_key, key_len);
		//strcpy(allocated_value, input_value);
		copy_from_user(allocated_value, input_value, value_len);
		//__clwb(allocated_value, strlen(input_value), insert_breakdown);
		__clwb(allocated_value, value_len, insert_breakdown);
		__clwb(allocated_key, key_len, insert_breakdown);
//		buffer_timestamp.et = mytimestamp();
//		timestamp_exec(&buffer_timestamp);
//		insert_breakdown->buffer_transfer.count++;
//		insert_breakdown->buffer_transfer.acc += buffer_timestamp.exec;
//		insert_breakdown->buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);


//		searching_timestamp.st = mytimestamp();
		allocated_key = (char *)((void *)allocated_key - pmem);
		allocated_value = (char *)((void *)allocated_value - pmem);
//		searching_timestamp.et = mytimestamp();
//		timestamp_exec(&searching_timestamp);
//		insert_breakdown->searching.count++;
//		insert_breakdown->searching.acc += searching_timestamp.exec;
//		insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);
		
		//tree modification breakdown
		addValue(input_node, allocated_key, allocated_value, NULL, insert_breakdown);

//		searching_timestamp.st = mytimestamp();

		//input_node = (void *)input_node + (size_t)pmem;

		if(currentNode == root) {
				up_write(&currentNode->node_rwsem);
				//bp_up_write(&currentNode->node_rwsem);
		}
		else {
				up_write(&currentNode->node_rwsem);
				//bp_up_write(&currentNode->node_rwsem);
				up_write(&parentNode->node_rwsem);
				//bp_up_write(&parentNode->node_rwsem);
		}
//		searching_timestamp.et = mytimestamp();
//		timestamp_exec(&searching_timestamp);
//		searching_timestamp.exec -= dup_total;
//		insert_breakdown->searching.count++;
//		insert_breakdown->searching.acc += searching_timestamp.exec;
//		insert_breakdown->searching.sqr_acc += (searching_timestamp.exec * searching_timestamp.exec);

		return 0;
}

int bp_remove(char *input_key, struct total_param *remove_breakdown) {
		struct bp_node *currentNode, *input_node;
		char *find_key;
		int index = 0;

		currentNode = (struct bp_node *)((void *)root - pmem);

		while(TRUE) {
				struct bp_node *input_currentNode;
				currentNode = (void *)currentNode + (size_t)pmem;
				input_currentNode = (struct bp_node *)((void *)currentNode - pmem);

				down_write(&currentNode->node_rwsem);
				//bp_down_write(&currentNode->node_rwsem);

				index = nextNodeIndex_search(input_currentNode, input_key, remove_breakdown);

				if(currentNode->is_leaf == TRUE) {
						break;
				}

				up_write(&currentNode->node_rwsem);
				//bp_up_write(&currentNode->node_rwsem);

				currentNode = currentNode->children[index];
		}

		input_node = (struct bp_node *)((void *)currentNode - pmem);

		find_key = (void *)currentNode->index[index] + (size_t)pmem;

		if(strcmp(find_key, input_key) != 0) {
				up_write(&currentNode->node_rwsem);
				//bp_up_write(&currentNode->node_rwsem);
				return -1;
		}

		else {
	//			down_write(&mr_rwsem);
				removeValue(input_node, index, remove_breakdown);
	//			up_write(&mr_rwsem);
				up_write(&currentNode->node_rwsem);
				//bp_up_write(&currentNode->node_rwsem);

				return 0;
		}
}


int bp_search(char *input_key, char __user *ret_value, struct total_param *search_breakdown) { 
		//=====
//		unsigned long long dup_st, dup_et, dup_exec, dup_total;
//		struct breakdown_timestamp search_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//		init_breakdown_timestamp(&search_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//		dup_total = 0;
		//=====
		char *find_key, *find_value;
		struct bp_node *currentNode;
		int index = 0;
		int ret;

		//=====
//		search_timestamp.st = mytimestamp();
		//=====

		currentNode = (struct bp_node *)((void *)root - pmem);

		while(TRUE) {
				struct bp_node *input_currentNode;
				currentNode = (void *)currentNode + (size_t)pmem;
				input_currentNode = (struct bp_node *)((void *)currentNode - pmem);

				down_read(&currentNode->node_rwsem);

//				dup_st = mytimestamp();
				index = nextNodeIndex_search(input_currentNode, input_key, search_breakdown);
//				dup_et = mytimestamp();
//				dup_exec = dup_et - dup_st;
//				dup_total += dup_exec;

				if(currentNode->is_leaf == TRUE) {
						break;
				}
				
				up_read(&currentNode->node_rwsem);
				//bp_up_read(&currentNode->node_rwsem);
				currentNode = currentNode->children[index];
		}

		find_key = (void *)currentNode->index[index] + (size_t)pmem;
		find_value = (void *)currentNode->children[index] + (size_t)pmem;

		if(strcmp(find_key, input_key) != 0) {
				up_read(&currentNode->node_rwsem);
				//bp_up_read(&currentNode->node_rwsem);
				//=====
//				search_timestamp.et = mytimestamp();
//				timestamp_exec(&search_timestamp);
//				search_timestamp.exec -= dup_total;
//				search_breakdown->searching.count++;
//				search_breakdown->searching.acc += search_timestamp.exec;
//				search_breakdown->searching.sqr_acc += (search_timestamp.exec * search_timestamp.exec);
				//=====

				return -1;
		}

		else {
//				buffer_timestamp.st = mytimestamp();
				ret = copy_to_user(ret_value, find_value, strlen(find_value));
//				buffer_timestamp.et = mytimestamp();
//				timestamp_exec(&buffer_timestamp);
//				search_breakdown->buffer_transfer.count++;
//				search_breakdown->buffer_transfer.acc += buffer_timestamp.exec;
//				search_breakdown->buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);
//
				up_read(&currentNode->node_rwsem);
				//bp_up_read(&currentNode->node_rwsem);

				//=====
//				search_timestamp.et = mytimestamp();
//				timestamp_exec(&search_timestamp);
//				search_timestamp.exec -= dup_total;
//				search_breakdown->searching.count++;
//				search_breakdown->searching.acc += search_timestamp.exec;
//				search_breakdown->searching.sqr_acc += (search_timestamp.exec * search_timestamp.exec);
				//=====

				return 0;
		}
}

//change allocation after search
SYSCALL_DEFINE4(btree_insert, char __user *, key, size_t, key_len, char __user *, value, size_t, value_len) {

		int i = 0;
		char *kernel_key = kzalloc(sizeof(char)*key_len, GFP_KERNEL);
		//char *kernel_value = kzalloc(sizeof(char)*value_len, GFP_KERNEL);

		int ret = 0;
		int index = 0;
		char *input_key = NULL; char *input_value = NULL;

		//=======================================================================
		struct total_param insert_breakdown;
//		struct breakdown_timestamp sys_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//		struct breakdown_timestamp bp_timestamp;
//		struct breakdown_timestamp wal_timestamp;
//
//		init_total_param(&insert_breakdown);
//		init_breakdown_timestamp(&sys_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//		init_breakdown_timestamp(&bp_timestamp);
//		init_breakdown_timestamp(&wal_timestamp);
//
//		sys_timestamp.st = mytimestamp();
//		//=======================================================================
	
//		buffer_timestamp.st = mytimestamp();
		ret = copy_from_user(kernel_key, key, key_len);
		//ret = copy_from_user(kernel_value, value, value_len);
//		buffer_timestamp.et = mytimestamp();
//		timestamp_exec(&buffer_timestamp);
//		insert_breakdown.buffer_transfer.acc += buffer_timestamp.exec;
//		insert_breakdown.buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);
//		insert_breakdown.buffer_transfer.count++;

//		bp_timestamp.st = mytimestamp();
		ret = bp_insert(kernel_key, key_len, value, value_len, &insert_breakdown);
//		bp_timestamp.et = mytimestamp();
//		timestamp_exec(&bp_timestamp);
//		insert_breakdown.bp.acc += bp_timestamp.exec;
//		insert_breakdown.bp.sqr_acc += (bp_timestamp.exec * bp_timestamp.exec);
//		insert_breakdown.bp.count++;

		init_recov();

		kfree(kernel_key);
		//kfree(kernel_value);

		//=======================================================================
//		sys_timestamp.et = mytimestamp();
//		timestamp_exec(&sys_timestamp);
//		insert_breakdown.sys.acc += sys_timestamp.exec;
//		insert_breakdown.sys.sqr_acc += (sys_timestamp.exec * sys_timestamp.exec);
//		insert_breakdown.sys.count++;


		if(ret == 0) {
				//update_global_stat(&global_breakdown_stat.bd_insert, &insert_breakdown);
		}
		if(ret == 1) {
				//update_global_stat(&global_breakdown_stat.bd_update, &insert_breakdown);
				if(is_alloc == 0) {
						int acc_count = 0;

						for(i = 0; i < 100; i++) {
								if(alloc_count[i] >0) {
										acc_count += alloc_count[i];
										printk(KERN_INFO "total count[%d] bytes: %d , acc: %lld\n", calc_size(i), alloc_count[i], acc_count);
								}
						}

						is_alloc = 1;
						for(i = 0; i < 100; i++) {
								alloc_count[i] = 0;
						}
				}
				else{
						//DO NOTHING
				}
		}
		//=======================================================================

		return ret;
}

//user can read value using ret_value 
SYSCALL_DEFINE3(btree_search, char __user *, key, size_t, key_len, char __user *, ret_value){

		//=======================================================================
		struct total_param search_breakdown;
//		struct breakdown_timestamp sys_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//
//		init_total_param(&search_breakdown);
//		init_breakdown_timestamp(&sys_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//
//		sys_timestamp.st = mytimestamp();
		//=======================================================================

		int i = 0;
		char *kernel_key = NULL;
		int ret = 0;

		//=====
//		sys_timestamp.st = mytimestamp();
		//=====

		//down_read(&mr_rwsem);
		kernel_key = kzalloc(key_len, GFP_KERNEL);

//		buffer_timestamp.st = mytimestamp();
		ret = copy_from_user(kernel_key, key, key_len);
//		buffer_timestamp.et = mytimestamp();

//		timestamp_exec(&buffer_timestamp);
//		search_breakdown.buffer_transfer.acc += buffer_timestamp.exec;
//		search_breakdown.buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);
//		search_breakdown.buffer_transfer.count++;

		ret = bp_search(kernel_key, ret_value, &search_breakdown);

		kfree(kernel_key);

		//=====
//		sys_timestamp.et = mytimestamp();
//		timestamp_exec(&sys_timestamp);
//		search_breakdown.sys.count++;
//		search_breakdown.sys.acc += sys_timestamp.exec;
//		search_breakdown.sys.sqr_acc += (sys_timestamp.exec * sys_timestamp.exec);

		if(is_alloc == 0) {
				int acc_count = 0;

				for(i = 0; i < 100; i++) {
						if(alloc_count[i] >0) {
								acc_count += alloc_count[i];
								printk(KERN_INFO "total count[%d] bytes: %d , acc: %lld\n", calc_size(i), alloc_count[i], acc_count);
						}
				}

				is_alloc = 1;
				for(i = 0; i < 100; i++) {
						alloc_count[i] = 0;
				}
		}
		else {
				//DO NOTHING
		}


				//update_global_stat(&global_breakdown_stat.bd_search, &search_breakdown);
		//=====
		//up_read(&mr_rwsem);
		return ret;
}

SYSCALL_DEFINE2(btree_remove, char __user *, key, size_t, key_len) {
		char *kernel_key = kzalloc(sizeof(char) * key_len, GFP_KERNEL);
		int ret = 0;

		//=======================================================================
		struct total_param remove_breakdown;
//		struct breakdown_timestamp sys_timestamp;
//		struct breakdown_timestamp buffer_timestamp;
//
//		init_total_param(&remove_breakdown);
//		init_breakdown_timestamp(&sys_timestamp);
//		init_breakdown_timestamp(&buffer_timestamp);
//
//		sys_timestamp.st = mytimestamp();
		//=======================================================================

		//down_write(&mr_rwsem);

//		buffer_timestamp.st = mytimestamp();

		ret = copy_from_user(kernel_key, key, key_len);

//		buffer_timestamp.et = mytimestamp();

//		timestamp_exec(&buffer_timestamp);
//		remove_breakdown.buffer_transfer.acc += buffer_timestamp.exec;
//		remove_breakdown.buffer_transfer.sqr_acc += (buffer_timestamp.exec * buffer_timestamp.exec);
//		remove_breakdown.buffer_transfer.count++;

		ret = bp_remove(kernel_key, &remove_breakdown);

		kfree(kernel_key);

		//up_write(&mr_rwsem);

		//=====
//		sys_timestamp.et = mytimestamp();
//		timestamp_exec(&sys_timestamp);
//		remove_breakdown.sys.count++;
//		remove_breakdown.sys.acc += sys_timestamp.exec;
//		remove_breakdown.sys.sqr_acc += (sys_timestamp.exec * sys_timestamp.exec);

//		update_global_stat(&global_breakdown_stat.bd_insert, &remove_breakdown);
		//=====


		return ret;
}

SYSCALL_DEFINE0(ink_init) {
		int i = 0;
		struct bp_node *root_offset = NULL;

		struct total_param temp;
		init_total_param(&temp);
		//=====
		/*
		   meta[0] : root_node
		   meta[1] : next_ptr
		   meta[2] : jour_addr offset
		   meta[3] : jour_curr offset
		   meta[4] : data_arr[]
		 */

		//bd_init();

		printk(KERN_ERR "\n=====INK_INIT=====\n");

		pmem = (void *)pmem_GetAddr();
		printk(KERN_ERR "pmem: %llx\n", (long long unsigned int)pmem);

		meta = (void **)pmem;
		root        = NULL;
		next_ptr = NULL;
		data_arr = NULL;

		jour_addr = (void *)PMEM_CAP + (size_t)pmem + 16;
		jour_curr = jour_addr;
		
		meta[1] = (void *)(1024L * 1024L * 1024L * 2L);
		//meta[1] = (void *)(1024L * 1024L * 1024L * 8L);
		meta[2] = (void *)jour_addr - (size_t)pmem;
		meta[3] = (void *)jour_curr - (size_t)pmem;

		printk(KERN_INFO "meta[2]: %llx, meta[3]: %llx\n", meta[2], meta[3]);

		next_ptr = meta[1] + (size_t)pmem;

		data_arr = (struct free_list *)&meta[4];
		for(i = 0; i < 512; i++) {
				data_arr[i].head = NULL;
				data_arr[i].tail = NULL;
				data_arr[i].nr_node = 0;
		}

		root_offset = bp_node_create(&temp);
		meta[0] = (void *)root_offset;
		root = meta[0] + (size_t)pmem;
		root->is_leaf = TRUE;

		init_rwsem(&list_rwsem);
		init_rwsem(&backup_rwsem);

		init_recov();

		printk(KERN_INFO "meta[3]: %llx, &meta[3]: %llx\n", meta[3], &meta[3]);

		printk(KERN_ERR "root: %llx next_ptr: %llx jour_addr: %llx jour_curr: %llx data_arr: %llx\n", (unsigned long long int)root, (unsigned long long int)next_ptr, (unsigned long long int)jour_addr, (unsigned long long int)jour_curr, (unsigned long long int)data_arr);

		printk(KERN_INFO "sizeof(struct bp_node): %d\n", sizeof(struct bp_node));
		printk(KERN_INFO "index: %d children: %d nr_chunks: %d is_leaf: %d rwsem: %d\n", offsetof(struct bp_node, index), offsetof(struct bp_node, children), offsetof(struct bp_node, nr_chunks), offsetof(struct bp_node, is_leaf), offsetof(struct bp_node, node_rwsem));
		printk(KERN_INFO "list_rwsem: %llx\n", &list_rwsem);
		printk(KERN_INFO "ENTRY LIMIT: %d\n", INDEX_MAX);

		//====================================================================================================
		//printk(KERN_INFO "THIS_MODULE: %s\n", THIS_MODULE);
		//ink_proc_file = proc_create("ink_proc_file", 0, NULL, &proc_file_fops);

		//ink_proc_file->read_proc = procfile_read;
		//ink_proc_file->write_proc = procfile_write;
		//ink_proc_file->owner = THIS_MODULE;
		//ink_proc_file->mode = S_IFREG | S_IRUGO;
		//====================================================================================================

		printk(KERN_INFO "meta[2]: %llx, meta[3]: %llx\n", meta[2], meta[3]);

		for(i = 0; i < 100; i++) {
				alloc_count[i] = 0;
		}
		is_alloc = 0;

		return 0;
}

SYSCALL_DEFINE0(ink_load) {
		int i = 0;
		size_t acc_count = 0;

		//bd_load();
		//bd_init();

		printk(KERN_ERR "\n=====INK_LOAD=====\n");

		pmem = (void *)pmem_GetAddr();

		meta           = NULL;
		root           = NULL;
		next_ptr       = NULL;
		data_arr       = NULL;
		jour_addr = NULL;
		jour_curr = NULL;

		meta           = (void **)pmem;
		root         	 = meta[0] + (size_t)pmem;
		next_ptr 	 = meta[1] + (size_t)pmem;
		jour_addr      = meta[2] + (size_t)pmem;
		jour_curr      = meta[3] + (size_t)pmem;
		data_arr       = (struct free_list *)&meta[4];

		init_rwsem(&list_rwsem);
		init_rwsem(&backup_rwsem);

		printk(KERN_INFO "meta[2]: %llx meta[3]: %llx\n", meta[2], meta[3]);
		printk(KERN_INFO "jour_addr: %llx jour_curr: %llx\n", jour_addr, jour_curr);
		//recovery_process();

		init_recov();

		//=====
		//=====

		printk(KERN_ERR "root: %llx next_ptr: %llx jour_addr: %llx jour_curr: %llx data_arr: %llx\n", (unsigned long long int)root, (unsigned long long int)next_ptr, (unsigned long long int)jour_addr, (unsigned long long int)jour_curr, (unsigned long long int)data_arr);
		printk(KERN_INFO "list_rwsem: %llx\n", &list_rwsem);
		printk(KERN_INFO "ENTRY LIMIT: %d\n", INDEX_MAX);

		//=============================================================
		for(i = 0; i < 25; i++) {
				if(data_arr[i].nr_node > 0)
						printk(KERN_INFO "%d bytes: %d\n", calc_size(i), data_arr[i].nr_node);
		}
		for(i = 0; i < 100; i++) {
				if(alloc_count[i] >0) {
						acc_count += alloc_count[i];
						printk(KERN_INFO "total count[%d] bytes: %d , acc: %lld\n", calc_size(i), alloc_count[i], acc_count);
				}
		}
		//=============================================================

		return 0;
}
