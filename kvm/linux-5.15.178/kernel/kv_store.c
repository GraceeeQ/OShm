#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>

struct my_data {
    int key;
    int value;
    struct hlist_node node;
};


SYSCALL_DEFINE2(write_kv, int, k, int, v)
{
	printf("write_kv: k=%d, v=%d\n", k, v);
	int hash = k % 1024;
    struct my_data *new_data = kmalloc(sizeof(struct my_data), GFP_KERNEL);
    new_data->key = key;
    new_data->value = value;

	spin_lock(&current->kv_store_lock[hash]);
	hlist_add_head(&new_data->node, &current->kv_store[hash]);
	spin_unlock(&current->kv_store_lock[hash]);
	return 0;
}

SYSCALL_DEFINE1(read_kv, int, k)
{
	printf("read_kv: k=%d\n", k);
	int hash = k % 1024;
    struct my_data *entry;
    int v=-2;
	spin_lock(&current->kv_store_lock[hash]);
    hlist_for_each_entry(entry, &current->kv_store[hash], node) {
        if (entry->key == k) {
            v= entry->value;
            break;
        }
    }
	spin_unlock(&current->kv_store_lock[hash]);
	return v;
}