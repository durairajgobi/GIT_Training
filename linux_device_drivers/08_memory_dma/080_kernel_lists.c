// SPDX-License-Identifier: GPL-2.0
/*
 * 080_kernel_lists.c - Kernel data structures: list_head, hlist hashtable, rbtree.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/rbtree.h>

struct item {
	int key;
	struct list_head lnode;
	struct hlist_node hnode;
	struct rb_node rbnode;
};

static LIST_HEAD(item_list);
static DEFINE_HASHTABLE(item_hash, 4);
static struct rb_root item_tree = RB_ROOT;

static void rb_insert_item(struct item *new)
{
	struct rb_node **link = &item_tree.rb_node, *parent = NULL;

	while (*link) {
		struct item *cur = rb_entry(*link, struct item, rbnode);

		parent = *link;
		link = new->key < cur->key ? &(*link)->rb_left : &(*link)->rb_right;
	}
	rb_link_node(&new->rbnode, parent, link);
	rb_insert_color(&new->rbnode, &item_tree);
}

static int __init ls_init(void)
{
	static const int keys[] = { 42, 7, 19, 3, 88, 25 };
	struct item *it;
	struct rb_node *n;
	int i;

	for (i = 0; i < ARRAY_SIZE(keys); i++) {
		it = kzalloc(sizeof(*it), GFP_KERNEL);
		if (!it)
			return -ENOMEM;	/* exit() frees whatever was added */
		it->key = keys[i];
		list_add_tail(&it->lnode, &item_list);
		hash_add(item_hash, &it->hnode, it->key);
		rb_insert_item(it);
	}

	list_for_each_entry(it, &item_list, lnode)
		pr_info("lists: list order %d\n", it->key);

	hash_for_each_possible(item_hash, it, hnode, 19)
		if (it->key == 19)
			pr_info("lists: hash lookup found 19\n");

	for (n = rb_first(&item_tree); n; n = rb_next(n))
		pr_info("lists: rbtree sorted %d\n", rb_entry(n, struct item, rbnode)->key);
	return 0;
}

static void __exit ls_exit(void)
{
	struct item *it, *tmp;

	list_for_each_entry_safe(it, tmp, &item_list, lnode) {
		list_del(&it->lnode);
		hash_del(&it->hnode);
		rb_erase(&it->rbnode, &item_tree);
		kfree(it);
	}
}

module_init(ls_init);
module_exit(ls_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel lists, hashtable and rbtree example");
