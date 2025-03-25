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
	int hash = k % 1024;
	struct my_data *new_data;
	printk(KERN_INFO "write_kv: k=%d, v=%d, hash=%d\n", k, v, hash);

	spin_lock(&current->kv_store_lock[hash]);
	hlist_for_each_entry (new_data, &current->kv_store[hash], node) {
		if (new_data->key == k) {
			new_data->value = v;
			spin_unlock(&current->kv_store_lock[hash]);
			return 0;
		}
	}
	new_data = kmalloc(sizeof(struct my_data), GFP_KERNEL);
    if (!new_data) {
        printk(KERN_ERR "write_kv: kmalloc failed for key=%d\n", k);
        spin_unlock(&current->kv_store_lock[hash]);
        return -ENOMEM;
    }
	new_data->key = k;
	new_data->value = v;
	hlist_add_head(&new_data->node, &current->kv_store[hash]);
	spin_unlock(&current->kv_store_lock[hash]);
	return 0;
}

SYSCALL_DEFINE1(read_kv, int, k)
{
	int hash = k % 1024;
	struct my_data *entry;
	int v = -2;
	// printk(KERN_INFO "read_kv: k=%d\n", k);
	spin_lock(&current->kv_store_lock[hash]);
    // printk(KERN_INFO "check1\n");
    if(hlist_empty(&current->kv_store[hash])){
        spin_unlock(&current->kv_store_lock[hash]);
        printk(KERN_INFO "when k = %d, hash = %d, table is empty\n",k,hash);
		v=-3;
		return -3;
    }
	hlist_for_each_entry (entry, &current->kv_store[hash], node) {
        // printk(KERN_INFO "check2\n");
		if (entry->key == k) {
			v = entry->value;
			// printk(KERN_INFO "read_kv: v=%d\n", v);
			break;
		}
	}
	spin_unlock(&current->kv_store_lock[hash]);
	return v;
}