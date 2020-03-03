/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  
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

  linux/include/linux/rbtree.h

  To use rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I know it's not the cleaner way,  but in C (not in C++) to get
  performances and genericity...

  See Documentation/rbtree.txt for documentation and samples.
*/

#ifndef	_LINUX_RBTREE_H
#define	_LINUX_RBTREE_H

#include <linux/kernel.h>
#include <linux/stddef.h>

/* 定义红黑树节点数据及其属性数据结构 */
struct rb_node {
	unsigned long  __rb_parent_color;  /* 指向当前节点的父节点并标识当前节点颜色 */
	struct rb_node *rb_right;          /* 指向当前节点的右子节点 */
	struct rb_node *rb_left;           /* 指向当前节点的左子节点 */
} __attribute__((aligned(sizeof(long))));
    /* The alignment might seem pointless, but allegedly CRIS needs it */

/* 定义红黑树根节点数据结构 */
struct rb_root {
	struct rb_node *rb_node;
};

/*********************************************************************************************************
** 函数名称: rb_parent
** 功能描述: 获取指定节点的父节点的指针
** 注     释: & ~3 操作是为了清除记录在 __rb_parent_color 变量中的颜色信息
** 输	 入: rb - 指定的节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define rb_parent(r)   ((struct rb_node *)((r)->__rb_parent_color & ~3))

/* 声明并初始化一个空的红黑树根节点 */
#define RB_ROOT	(struct rb_root) { NULL, }

/*********************************************************************************************************
** 函数名称: rb_entry
** 功能描述: 根据指定的红黑树节点地址指针计算出节点所在结构体的首地址
** 输	 入: ptr - 指定的节点指针
**         : type - 节点所在结构体类型
**         : member - 节点在结构体中的名字
** 输	 出: type * - 结构体的首地址
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define	rb_entry(ptr, type, member) container_of(ptr, type, member)

/*********************************************************************************************************
** 函数名称: RB_EMPTY_ROOT
** 功能描述: 设置指定的红黑树为空
** 输	 入: root - 指定的红黑树根节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define RB_EMPTY_ROOT(root)  ((root)->rb_node == NULL)

/* 'empty' nodes are nodes that are known not to be inserted in an rbtree */
/*********************************************************************************************************
** 函数名称: RB_EMPTY_NODE
** 功能描述: 判断指定的红黑树节点是否为空，即父节点是否指向自己
** 输	 入: node - 指定的节点指针
** 输	 出: 1 - 为空
**         : 0 - 不为空
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define RB_EMPTY_NODE(node)  \
	((node)->__rb_parent_color == (unsigned long)(node))

/*********************************************************************************************************
** 函数名称: RB_CLEAR_NODE
** 功能描述: 设置指定的红黑树节点为空状态，即父节点指向自己
** 输	 入: node - 指定的节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define RB_CLEAR_NODE(node)  \
	((node)->__rb_parent_color = (unsigned long)(node))

extern void rb_insert_color(struct rb_node *, struct rb_root *);
extern void rb_erase(struct rb_node *, struct rb_root *);

/* Find logical next and previous nodes in a tree */
extern struct rb_node *rb_next(const struct rb_node *);
extern struct rb_node *rb_prev(const struct rb_node *);
extern struct rb_node *rb_first(const struct rb_root *);
extern struct rb_node *rb_last(const struct rb_root *);

/* Postorder iteration - always visit the parent after its children */
extern struct rb_node *rb_first_postorder(const struct rb_root *);
extern struct rb_node *rb_next_postorder(const struct rb_node *);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
extern void rb_replace_node(struct rb_node *victim, struct rb_node *new, 
			    struct rb_root *root);

/*********************************************************************************************************
** 函数名称: rb_link_node
** 功能描述: 把指定的节点插入到红黑树的指定位置处
** 输	 入: node - 待插入的节点指针
**         : parent - 待插入节点的父节点指针
**         : rb_link - 指向待插入节点的二级指针，例如 &parent->rb_left 或者 &parent->rb_right
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void rb_link_node(struct rb_node * node, struct rb_node * parent,
				struct rb_node ** rb_link)
{
	node->__rb_parent_color = (unsigned long)parent;
	node->rb_left = node->rb_right = NULL;

	*rb_link = node;
}

/*********************************************************************************************************
** 函数名称: rb_entry_safe
** 功能描述: 根据指定的红黑树节点地址指针计算出节点所在结构体的首地址
** 输	 入: ptr - 指定的节点指针
**         : type - 节点所在结构体类型
**         : member - 节点在结构体中的名字
** 输	 出: type * - 结构体的首地址
**         : NULL - 参数错误
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define rb_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   ____ptr ? rb_entry(____ptr, type, member) : NULL; \
	})

/**
 * rbtree_postorder_for_each_entry_safe - iterate over rb_root in post order of
 * given type safe against removal of rb_node entry
 *
 * @pos:	the 'type *' to use as a loop cursor.
 * @n:		another 'type *' to use as temporary storage
 * @root:	'rb_root *' of the rbtree.
 * @field:	the name of the rb_node field within 'type'.
 */
/*********************************************************************************************************
** 函数名称: rbtree_postorder_for_each_entry_safe
** 功能描述: 根据指定的参数在指定红黑树上按照从小到大的顺序遍历每一个对象成员
** 输	 入: pos - 遍历操作过程中使用的 type * 类型的对象指针变量
**         : n - 遍历过程使用的 type * 类型的临时对象指针变量
**         : root - 需要遍历的红黑树根节点指针
**         : field - 红黑树节点在所属对象结果体中的变量名
** 输	 出: pos - 遍历过程中可操作的对象指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
	for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
	     pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field), \
			typeof(*pos), field); 1; }); \
	     pos = n)

#endif	/* _LINUX_RBTREE_H */
