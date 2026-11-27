// SPDX-License-Identifier: GPL-2.0
/*
 * lfd_buggy - the "inventory" driver delivered by a contractor (Lab 02).
 *
 * Control file /sys/kernel/debug/lfd_buggy/ctl takes one command per write:
 *
 *   add NAME          create an item (names are at most 31 characters)
 *   del NAME          delete an item
 *   find NAME         log the item's id
 *   tag NAME TAG      attach a tag string to an item
 *   rename OLD NEW    rename an item
 *
 * /sys/kernel/debug/lfd_buggy/items lists "id name tag" lines, and
 * /sys/kernel/debug/lfd_buggy/stats shows how many commands were handled.
 *
 * Customers report crashes and memory growth.  Your job (Lab 02) is to
 * find each defect with the right tool, explain it, and fix it.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>

#define NAME_LEN 32

struct item {
	struct list_head node;
	char *name;
	char *tag;
	int id;
};

/*
 * The command handlers are noinline so that each one appears in stack
 * traces and in ftrace under its own name.
 */
static LIST_HEAD(items);
static DEFINE_MUTEX(items_lock);	/* protects items and next_id */
static int next_id = 1;

/* Command history: one timestamp per handled command. */
static DEFINE_MUTEX(stats_lock);
static unsigned long *history;
static size_t history_len, history_capacity;

static struct dentry *dir;

static void record_command(void)
{
	unsigned long *h;

	mutex_lock(&stats_lock);
	if (history_len == history_capacity) {
		h = krealloc(history, (history_capacity + 16) * sizeof(*h), GFP_KERNEL);
		if (h) {
			history = h;
			history_capacity += 16;
		}
	}
	if (history_len < history_capacity)
		history[history_len++] = jiffies;
	mutex_unlock(&stats_lock);
}

/* Caller holds items_lock.  Returns NULL if there is no such item. */
static struct item *find_item(const char *name)
{
	struct item *it;

	list_for_each_entry(it, &items, node)
		if (!strcmp(it->name, name))
			return it;
	return NULL;
}

static noinline int cmd_add(const char *name)
{
	size_t len = strlen(name);
	struct item *it;

	it = kzalloc(sizeof(*it), GFP_KERNEL);
	if (!it)
		return -ENOMEM;
	it->name = kmalloc(len + 1, GFP_KERNEL);
	if (!it->name) {
		kfree(it);
		return -ENOMEM;
	}
	strcpy(it->name, name);

	mutex_lock(&items_lock);
	if (find_item(name)) {
		mutex_unlock(&items_lock);
		kfree(it->name);
		kfree(it);
		return -EEXIST;
	}
	it->id = next_id++;
	list_add_tail(&it->node, &items);
	mutex_unlock(&items_lock);
	pr_debug("added %s as %d\n", it->name, it->id);
	return 0;
}

static noinline int cmd_del(const char *name)
{
	struct item *it;

	mutex_lock(&items_lock);
	it = find_item(name);
	if (!it) {
		mutex_unlock(&items_lock);
		return -ENOENT;
	}
	list_del(&it->node);
	mutex_unlock(&items_lock);
	pr_info("deleted %s (id %d)\n", it->name, it->id);

	kfree(it->tag);
	kfree(it->name);
	kfree(it);
	return 0;
}

static noinline int cmd_find(const char *name)
{
	struct item *it;

	mutex_lock(&items_lock);
	it = find_item(name);
	if (!it) {
		mutex_unlock(&items_lock);
		return -ENOENT;
	}
	mutex_unlock(&items_lock);
	pr_info("%s has id %d\n", it->name, it->id);
	return 0;
}

static noinline int cmd_tag(const char *name, const char *tag)
{
	struct item *it;
	char *t;
	size_t tag_len = strlen(tag);

	t = kmalloc(tag_len + 1, GFP_KERNEL);
	if (!t) 
		return -ENOMEM;

	strcpy(t, tag);

	mutex_lock(&items_lock);
	it = find_item(name);
	if (!it) {
		mutex_unlock(&items_lock);
		kfree(t);		
		return -ENOENT;
	}

	if (it->tag) kfree(it->tag);
	it->tag = t;
	mutex_unlock(&items_lock);

	return 0;
}

static noinline int cmd_rename(const char *old, const char *new)
{
	struct item *it;
	char *n;

	n = kstrdup(new, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	mutex_lock(&items_lock);
	it = find_item(old);
	if (!it) {
		kfree(n);
		mutex_unlock(&items_lock);
		return -ENOENT;
	}
	kfree(it->name);
	it->name = n;
	mutex_unlock(&items_lock);
	return 0;
}

static ssize_t ctl_write(struct file *file, const char __user *ubuf,
			 size_t count, loff_t *ppos)
{
	char cmd[8], a[NAME_LEN], b[NAME_LEN];
	char *buf;
	int n, ret;

	if (count > 128)
		return -EINVAL;
	buf = memdup_user_nul(ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	n = sscanf(buf, "%7s %31s %31s", cmd, a, b);
	kfree(buf);
	pr_debug("command '%s' with %d words\n", n > 0 ? cmd : "", n);

	if (n == 2 && !strcmp(cmd, "add"))
		ret = cmd_add(a);
	else if (n == 2 && !strcmp(cmd, "del"))
		ret = cmd_del(a);
	else if (n == 2 && !strcmp(cmd, "find"))
		ret = cmd_find(a);
	else if (n == 3 && !strcmp(cmd, "tag"))
		ret = cmd_tag(a, b);
	else if (n == 3 && !strcmp(cmd, "rename"))
		ret = cmd_rename(a, b);
	else
		ret = -EINVAL;

	record_command();
	return ret ? ret : count;
}

static const struct file_operations ctl_fops = {
	.owner = THIS_MODULE,
	.write = ctl_write,
};

static int items_show(struct seq_file *m, void *v)
{
	struct item *it;

	mutex_lock(&items_lock);
	list_for_each_entry(it, &items, node)
		seq_printf(m, "%d %s %s\n", it->id, it->name, it->tag ?: "-");
	mutex_unlock(&items_lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(items);

static int stats_show(struct seq_file *m, void *v)
{
	size_t len;

	mutex_lock(&stats_lock);
	len = history_len;
	mutex_unlock(&stats_lock);
	seq_printf(m, "commands %zu\n", len);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(stats);

static int __init lfd_buggy_init(void)
{
	dir = debugfs_create_dir("lfd_buggy", NULL);
	debugfs_create_file("ctl", 0200, dir, NULL, &ctl_fops);
	debugfs_create_file("items", 0444, dir, NULL, &items_fops);
	debugfs_create_file("stats", 0444, dir, NULL, &stats_fops);
	pr_info("loaded\n");
	return 0;
}

static void __exit lfd_buggy_exit(void)
{
	struct item *it, *tmp;

	debugfs_remove_recursive(dir);
	list_for_each_entry_safe(it, tmp, &items, node) {
		list_del(&it->node);
		kfree(it->tag);
		kfree(it->name);
		kfree(it);
	}
	kfree(history);
	pr_info("unloaded\n");
}

module_init(lfd_buggy_init);
module_exit(lfd_buggy_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Inventory driver with field-reported defects (LFD430 Lab 02)");
