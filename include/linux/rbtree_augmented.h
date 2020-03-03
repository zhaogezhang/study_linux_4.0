/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  (C) 2012  Michel Lespinasse <walken@google.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  linux/include/linux/rbtree_augmented.h
*/

#ifndef _LINUX_RBTREE_AUGMENTED_H
#define _LINUX_RBTREE_AUGMENTED_H

#include <linux/compiler.h>
#include <linux/rbtree.h>

/*
 * Please note - only struct rb_augment_callbacks and the prototypes for
 * rb_insert_augmented() and rb_erase_augmented() are intended to be public.
 * The rest are implementation details you are not expected to depend on.
 *
 * See Documentation/rbtree.txt for documentation and samples.
 */

struct rb_augment_callbacks {
	void (*propagate)(struct rb_node *node, struct rb_node *stop);
	void (*copy)(struct rb_node *old, struct rb_node *new);
	void (*rotate)(struct rb_node *old, struct rb_node *new);
};

extern void __rb_insert_augmented(struct rb_node *node, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));
/*
 * Fixup the rbtree and update the augmented information when rebalancing.
 *
 * On insertion, the user must update the augmented information on the path
 * leading to the inserted node, then call rb_link_node() as usual and
 * rb_augment_inserted() instead of the usual rb_insert_color() call.
 * If rb_augment_inserted() rebalances the rbtree, it will callback into
 * a user provided function to update the augmented information on the
 * affected subtrees.
 */
static inline void
rb_insert_augmented(struct rb_node *node, struct rb_root *root,
		    const struct rb_augment_callbacks *augment)
{
	__rb_insert_augmented(node, root, augment->rotate);
}

#define RB_DECLARE_CALLBACKS(rbstatic, rbname, rbstruct, rbfield,	\
			     rbtype, rbaugmented, rbcompute)		\
static inline void							\
rbname ## _propagate(struct rb_node *rb, struct rb_node *stop)		\
{									\
	while (rb != stop) {						\
		rbstruct *node = rb_entry(rb, rbstruct, rbfield);	\
		rbtype augmented = rbcompute(node);			\
		if (node->rbaugmented == augmented)			\
			break;						\
		node->rbaugmented = augmented;				\
		rb = rb_parent(&node->rbfield);				\
	}								\
}									\
static inline void							\
rbname ## _copy(struct rb_node *rb_old, struct rb_node *rb_new)		\
{									\
	rbstruct *old = rb_entry(rb_old, rbstruct, rbfield);		\
	rbstruct *new = rb_entry(rb_new, rbstruct, rbfield);		\
	new->rbaugmented = old->rbaugmented;				\
}									\
static void								\
rbname ## _rotate(struct rb_node *rb_old, struct rb_node *rb_new)	\
{									\
	rbstruct *old = rb_entry(rb_old, rbstruct, rbfield);		\
	rbstruct *new = rb_entry(rb_new, rbstruct, rbfield);		\
	new->rbaugmented = old->rbaugmented;				\
	old->rbaugmented = rbcompute(old);				\
}									\
rbstatic const struct rb_augment_callbacks rbname = {			\
	rbname ## _propagate, rbname ## _copy, rbname ## _rotate	\
};

#define	RB_RED		0
#define	RB_BLACK	1

#define __rb_parent(pc)    ((struct rb_node *)(pc & ~3))

/*********************************************************************************************************
** 函数名称: __rb_color
** 功能描述: 获取指定变量中的颜色域信息
** 输	 入: pc - 指定的变量值
** 输	 出: 获取到的颜色域信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __rb_color(pc)     ((pc) & 1)

/*********************************************************************************************************
** 函数名称: __rb_is_black
** 功能描述: 判断指定变量中是否包含黑色标志信心
** 输	 入: pc - 指定的变量值
** 输	 出: 1 - 包含黑色标志
**         : 0 - 不包含黑色标志
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __rb_is_black(pc)  __rb_color(pc)

/*********************************************************************************************************
** 函数名称: __rb_is_red
** 功能描述: 判断指定变量中是否包含红色标志信心
** 输	 入: pc - 指定的变量值
** 输	 出: 1 - 包含红色标志
**         : 0 - 不包含红色标志
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __rb_is_red(pc)    (!__rb_color(pc))

/*********************************************************************************************************
** 函数名称: rb_color
** 功能描述: 获取指定节点的颜色域信息
** 输	 入: rb - 指定的节点指针
** 输	 出: 获取到的颜色域信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define rb_color(rb)       __rb_color((rb)->__rb_parent_color)

/*********************************************************************************************************
** 函数名称: rb_is_red
** 功能描述: 判断指定的节点颜色是否为红色
** 输	 入: rb - 指定的节点指针
** 输	 出: 1 - 是红色
**         : 0 - 不是红色
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define rb_is_red(rb)      __rb_is_red((rb)->__rb_parent_color)

/*********************************************************************************************************
** 函数名称: rb_is_black
** 功能描述: 判断指定的节点颜色是否为黑色
** 输	 入: rb - 指定的节点指针
** 输	 出: 1 - 是黑色
**         : 0 - 不是黑色
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define rb_is_black(rb)    __rb_is_black((rb)->__rb_parent_color)

/*********************************************************************************************************
** 函数名称: rb_set_parent
** 功能描述: 设置指定节点的父节点指针
** 输	 入: rb - 指定的节点指针
**         : p - 指定的父节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void rb_set_parent(struct rb_node *rb, struct rb_node *p)
{
	rb->__rb_parent_color = rb_color(rb) | (unsigned long)p;
}

/*********************************************************************************************************
** 函数名称: rb_set_parent_color
** 功能描述: 设置指定节点的父节点指针以及这个节点的颜色
** 输	 入: rb - 指定的节点指针
**         : p - 父节点指针
**         : color - 当前节点的颜色
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void rb_set_parent_color(struct rb_node *rb,
				       struct rb_node *p, int color)
{
	rb->__rb_parent_color = (unsigned long)p | color;
}

/*********************************************************************************************************
** 函数名称: __rb_change_child
** 功能描述: 把指定父节点的子节点（左子节点或者右子节点）从指定的旧节点改成指定的新节点
** 注     释: 如果没指定父节点指针，即 parent == NULL 则表示新节点为根节点
** 输	 入: old - 指定的旧子节点指针
**         : new - 指定的新子节点指针
**         : parent - 指定节点的父节点指针
**         : root - 指定节点所在红黑树的根节点
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
__rb_change_child(struct rb_node *old, struct rb_node *new,
		  struct rb_node *parent, struct rb_root *root)
{
	if (parent) {
		if (parent->rb_left == old)
			parent->rb_left = new;
		else
			parent->rb_right = new;
	} else
		root->rb_node = new;
}

extern void __rb_erase_color(struct rb_node *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new));

/*********************************************************************************************************
** 函数名称: __rb_erase_augmented
** 功能描述: 把指定的节点从指定的红黑树上移除
** 输	 入: parent - 指定的待移除节点指针
**         : root - 指定的红黑树根节点指针
**         : augment - 
** 输	 出: rebalance - 移除节点的父节点指针且需要重新自平衡
**         : NULL - 移除节点后不需要重新自平衡
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline struct rb_node *
__rb_erase_augmented(struct rb_node *node, struct rb_root *root,
		     const struct rb_augment_callbacks *augment)
{
	struct rb_node *child = node->rb_right, *tmp = node->rb_left;
	struct rb_node *parent, *rebalance;
	unsigned long pc;

	if (!tmp) {
		/*
		 * Case 1: node to erase has no more than 1 child (easy!)
		 *
		 * Note that if there is one child it must be red due to 5)
		 * and node must be black due to 4). We adjust colors locally
		 * so as to bypass __rb_erase_color() later on.
		 */
		pc = node->__rb_parent_color;
		parent = __rb_parent(pc);
		__rb_change_child(node, child, parent, root);
		if (child) {
			child->__rb_parent_color = pc;
			rebalance = NULL;
		} else
			rebalance = __rb_is_black(pc) ? parent : NULL;
		tmp = parent;
	} else if (!child) {
		/* Still case 1, but this time the child is node->rb_left */
		tmp->__rb_parent_color = pc = node->__rb_parent_color;
		parent = __rb_parent(pc);
		__rb_change_child(node, tmp, parent, root);
		rebalance = NULL;
		tmp = parent;
	} else {
		struct rb_node *successor = child, *child2;
		tmp = child->rb_left;
		if (!tmp) {
			/*
			 * Case 2: node's successor is its right child
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (s)  ->  (x) (c)
			 *        \
			 *        (c)
			 */
			parent = successor;
			child2 = successor->rb_right;
			augment->copy(node, successor);
		} else {
			/*
			 * Case 3: node's successor is leftmost under
			 * node's right child subtree
			 *
			 *    (n)          (s)
			 *    / \          / \
			 *  (x) (y)  ->  (x) (y)
			 *      /            /
			 *    (p)          (p)
			 *    /            /
			 *  (s)          (c)
			 *    \
			 *    (c)
			 */
			do {
				parent = successor;
				successor = tmp;
				tmp = tmp->rb_left;
			} while (tmp);
			parent->rb_left = child2 = successor->rb_right;
			successor->rb_right = child;
			rb_set_parent(child, successor);
			augment->copy(node, successor);
			augment->propagate(parent, successor);
		}

		successor->rb_left = tmp = node->rb_left;
		rb_set_parent(tmp, successor);

		pc = node->__rb_parent_color;
		tmp = __rb_parent(pc);
		__rb_change_child(node, successor, tmp, root);
		if (child2) {
			successor->__rb_parent_color = pc;
			rb_set_parent_color(child2, parent, RB_BLACK);
			rebalance = NULL;
		} else {
			unsigned long pc2 = successor->__rb_parent_color;
			successor->__rb_parent_color = pc;
			rebalance = __rb_is_black(pc2) ? parent : NULL;
		}
		tmp = successor;
	}

	augment->propagate(tmp, NULL);
	return rebalance;
}

/*********************************************************************************************************
** 函数名称: rb_erase_augmented
** 功能描述: 把指定的节点从指定的红黑树上移除并处理树的平衡和节点颜色
** 输	 入: node - 指定的节点指针
**         : root - 指定的红黑树根节点指针
**         : augment - 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void
rb_erase_augmented(struct rb_node *node, struct rb_root *root,
		   const struct rb_augment_callbacks *augment)
{
	struct rb_node *rebalance = __rb_erase_augmented(node, root, augment);
	if (rebalance)
		__rb_erase_color(rebalance, root, augment->rotate);
}

#endif	/* _LINUX_RBTREE_AUGMENTED_H */
