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

  linux/lib/rbtree.c
*/

#include <linux/rbtree_augmented.h>
#include <linux/export.h>

/*
 * red-black trees properties:  http://en.wikipedia.org/wiki/Rbtree
 *
 *  1) A node is either red or black
 *  2) The root is black
 *  3) All leaves (NULL) are black
 *  4) Both children of every red node are black
 *  5) Every simple path from root to leaves contains the same number
 *     of black nodes.
 *
 *  4 and 5 give the O(log n) guarantee, since 4 implies you cannot have two
 *  consecutive red nodes in a path and every red node is therefore followed by
 *  a black. So if B is the number of black nodes on every simple path (as per
 *  5), then the longest possible path due to 4 is 2B.
 *
 *  We shall indicate color with case, where black nodes are uppercase and red
 *  nodes will be lowercase. Unknown color nodes shall be drawn as red within
 *  parentheses and have some accompanying text comment.
 */
/*********************************************************************************************************
** 函数名称: rb_set_black
** 功能描述: 设置指定的节点颜色为黑色
** 输	 入: rb - 指定的节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void rb_set_black(struct rb_node *rb)
{
	rb->__rb_parent_color |= RB_BLACK;
}

/*********************************************************************************************************
** 函数名称: rb_red_parent
** 功能描述: 获取指定红色节点的父节点的指针
** 注     释: 因为指定的节点是红色，所以没有颜色信息，所以不需要执行 & ~3 操作
** 输	 入: rb - 指定的节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct rb_node *rb_red_parent(struct rb_node *red)
{
	return (struct rb_node *)red->__rb_parent_color;
}

/*
 * Helper function for rotations:
 * - old's parent and color get assigned to new
 * - old gets assigned new as a parent and 'color' as a color.
 */
/*********************************************************************************************************
** 函数名称: __rb_rotate_set_parents
** 功能描述: 在指定的红黑树上把指定的新节点插入到指定的旧节点和这个旧节点的父节点之间，并为
**         : 旧节点指定新的颜色信息
** 输	 入: old - 指定的旧节点指针
**         : new - 指定的新节点指针
**         : root - 指定的红黑树根节点
**         : color - 为旧节点指定的新颜色
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
__rb_rotate_set_parents(struct rb_node *old, struct rb_node *new,
			struct rb_root *root, int color)
{
	struct rb_node *parent = rb_parent(old);

	/* new->parent_color = old->parent_color */
	new->__rb_parent_color = old->__rb_parent_color;

	/* old->parent = new, old->color = color */
	rb_set_parent_color(old, new, color);

	/* parent->child = new */
	__rb_change_child(old, new, parent, root);
}

/*********************************************************************************************************
** 函数名称: __rb_insert
** 功能描述: 把指定的节点插入到指定的红黑树上之后用来处理树的平衡和节点颜色
** 输	 入: node - 指定的插入节点指针
**         : root - 指定的红黑树根节点指针
**         : augment_rotate - 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void
__rb_insert(struct rb_node *node, struct rb_root *root,
	    void (*augment_rotate)(struct rb_node *old, struct rb_node *new))
{
	struct rb_node *parent = rb_red_parent(node), *gparent, *tmp;

	while (true) {
		/*
		 * Loop invariant: node is red
		 *
		 * If there is a black parent, we are done.
		 * Otherwise, take some corrective action as we don't
		 * want a red root or two consecutive red nodes.
		 */
		if (!parent) {
			rb_set_parent_color(node, NULL, RB_BLACK);
			break;
		} else if (rb_is_black(parent))
			break;

		gparent = rb_red_parent(parent);

		tmp = gparent->rb_right;
		if (parent != tmp) {	/* parent == gparent->rb_left */
			if (tmp && rb_is_red(tmp)) {
				/*
				 * Case 1 - color flips
				 *
				 *       G            g
				 *      / \          / \
				 *     p   u  -->   P   U
				 *    /            /
				 *   n            n
				 *
				 * However, since g's parent might be red, and
				 * 4) does not allow this, we need to recurse
				 * at g.
				 */
				rb_set_parent_color(tmp, gparent, RB_BLACK);
				rb_set_parent_color(parent, gparent, RB_BLACK);
				node = gparent;
				parent = rb_parent(node);
				rb_set_parent_color(node, parent, RB_RED);
				continue;
			}

			tmp = parent->rb_right;
			if (node == tmp) {
				/*
				 * Case 2 - left rotate at parent
				 *
				 *      G             G
				 *     / \           / \
				 *    p   U  -->    n   U
				 *     \           /
				 *      n         p
				 *
				 * This still leaves us in violation of 4), the
				 * continuation into Case 3 will fix that.
				 */
				parent->rb_right = tmp = node->rb_left;
				node->rb_left = parent;
				if (tmp)
					rb_set_parent_color(tmp, parent,
							    RB_BLACK);
				rb_set_parent_color(parent, node, RB_RED);
				augment_rotate(parent, node);
				parent = node;
				tmp = node->rb_right;
			}

			/*
			 * Case 3 - right rotate at gparent
			 *
			 *        G           P
			 *       / \         / \
			 *      p   U  -->  n   g
			 *     /                 \
			 *    n                   U
			 */
			gparent->rb_left = tmp;  /* == parent->rb_right */
			parent->rb_right = gparent;
			if (tmp)
				rb_set_parent_color(tmp, gparent, RB_BLACK);
			__rb_rotate_set_parents(gparent, parent, root, RB_RED);
			augment_rotate(gparent, parent);
			break;
		} else {
			tmp = gparent->rb_left;
			if (tmp && rb_is_red(tmp)) {
				/* Case 1 - color flips */
				rb_set_parent_color(tmp, gparent, RB_BLACK);
				rb_set_parent_color(parent, gparent, RB_BLACK);
				node = gparent;
				parent = rb_parent(node);
				rb_set_parent_color(node, parent, RB_RED);
				continue;
			}

			tmp = parent->rb_left;
			if (node == tmp) {
				/* Case 2 - right rotate at parent */
				parent->rb_left = tmp = node->rb_right;
				node->rb_right = parent;
				if (tmp)
					rb_set_parent_color(tmp, parent,
							    RB_BLACK);
				rb_set_parent_color(parent, node, RB_RED);
				augment_rotate(parent, node);
				parent = node;
				tmp = node->rb_left;
			}

			/* Case 3 - left rotate at gparent */
			gparent->rb_right = tmp;  /* == parent->rb_left */
			parent->rb_left = gparent;
			if (tmp)
				rb_set_parent_color(tmp, gparent, RB_BLACK);
			__rb_rotate_set_parents(gparent, parent, root, RB_RED);
			augment_rotate(gparent, parent);
			break;
		}
	}
}

/*
 * Inline version for rb_erase() use - we want to be able to inline
 * and eliminate the dummy_rotate callback there
 */
/*********************************************************************************************************
** 函数名称: ____rb_erase_color
** 功能描述: 把指定的节点从指定的红黑树上移除之后用来处理树的平衡和节点颜色
** 输	 入: parent - 移除节点的父节点指针
**         : root - 指定的红黑树根节点指针
**         : augment_rotate - 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void
____rb_erase_color(struct rb_node *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new))
{
	struct rb_node *node = NULL, *sibling, *tmp1, *tmp2;

	while (true) {
		/*
		 * Loop invariants:
		 * - node is black (or NULL on first iteration)
		 * - node is not the root (parent is not NULL)
		 * - All leaf paths going through parent and node have a
		 *   black node count that is 1 lower than other leaf paths.
		 */
		sibling = parent->rb_right;
		if (node != sibling) {	/* node == parent->rb_left */
			if (rb_is_red(sibling)) {
				/*
				 * Case 1 - left rotate at parent
				 *
				 *     P               S
				 *    / \             / \
				 *   N   s    -->    p   Sr
				 *      / \         / \
				 *     Sl  Sr      N   Sl
				 */
				parent->rb_right = tmp1 = sibling->rb_left;
				sibling->rb_left = parent;
				rb_set_parent_color(tmp1, parent, RB_BLACK);
				__rb_rotate_set_parents(parent, sibling, root,
							RB_RED);
				augment_rotate(parent, sibling);
				sibling = tmp1;
			}
			tmp1 = sibling->rb_right;
			if (!tmp1 || rb_is_black(tmp1)) {
				tmp2 = sibling->rb_left;
				if (!tmp2 || rb_is_black(tmp2)) {
					/*
					 * Case 2 - sibling color flip
					 * (p could be either color here)
					 *
					 *    (p)           (p)
					 *    / \           / \
					 *   N   S    -->  N   s
					 *      / \           / \
					 *     Sl  Sr        Sl  Sr
					 *
					 * This leaves us violating 5) which
					 * can be fixed by flipping p to black
					 * if it was red, or by recursing at p.
					 * p is red when coming from Case 1.
					 */
					rb_set_parent_color(sibling, parent,
							    RB_RED);
					if (rb_is_red(parent))
						rb_set_black(parent);
					else {
						node = parent;
						parent = rb_parent(node);
						if (parent)
							continue;
					}
					break;
				}
				/*
				 * Case 3 - right rotate at sibling
				 * (p could be either color here)
				 *
				 *   (p)           (p)
				 *   / \           / \
				 *  N   S    -->  N   Sl
				 *     / \             \
				 *    sl  Sr            s
				 *                       \
				 *                        Sr
				 */
				sibling->rb_left = tmp1 = tmp2->rb_right;
				tmp2->rb_right = sibling;
				parent->rb_right = tmp2;
				if (tmp1)
					rb_set_parent_color(tmp1, sibling,
							    RB_BLACK);
				augment_rotate(sibling, tmp2);
				tmp1 = sibling;
				sibling = tmp2;
			}
			/*
			 * Case 4 - left rotate at parent + color flips
			 * (p and sl could be either color here.
			 *  After rotation, p becomes black, s acquires
			 *  p's color, and sl keeps its color)
			 *
			 *      (p)             (s)
			 *      / \             / \
			 *     N   S     -->   P   Sr
			 *        / \         / \
			 *      (sl) sr      N  (sl)
			 */
			parent->rb_right = tmp2 = sibling->rb_left;
			sibling->rb_left = parent;
			rb_set_parent_color(tmp1, sibling, RB_BLACK);
			if (tmp2)
				rb_set_parent(tmp2, parent);
			__rb_rotate_set_parents(parent, sibling, root,
						RB_BLACK);
			augment_rotate(parent, sibling);
			break;
		} else {
			sibling = parent->rb_left;
			if (rb_is_red(sibling)) {
				/* Case 1 - right rotate at parent */
				parent->rb_left = tmp1 = sibling->rb_right;
				sibling->rb_right = parent;
				rb_set_parent_color(tmp1, parent, RB_BLACK);
				__rb_rotate_set_parents(parent, sibling, root,
							RB_RED);
				augment_rotate(parent, sibling);
				sibling = tmp1;
			}
			tmp1 = sibling->rb_left;
			if (!tmp1 || rb_is_black(tmp1)) {
				tmp2 = sibling->rb_right;
				if (!tmp2 || rb_is_black(tmp2)) {
					/* Case 2 - sibling color flip */
					rb_set_parent_color(sibling, parent,
							    RB_RED);
					if (rb_is_red(parent))
						rb_set_black(parent);
					else {
						node = parent;
						parent = rb_parent(node);
						if (parent)
							continue;
					}
					break;
				}
				/* Case 3 - right rotate at sibling */
				sibling->rb_right = tmp1 = tmp2->rb_left;
				tmp2->rb_left = sibling;
				parent->rb_left = tmp2;
				if (tmp1)
					rb_set_parent_color(tmp1, sibling,
							    RB_BLACK);
				augment_rotate(sibling, tmp2);
				tmp1 = sibling;
				sibling = tmp2;
			}
			/* Case 4 - left rotate at parent + color flips */
			parent->rb_left = tmp2 = sibling->rb_right;
			sibling->rb_right = parent;
			rb_set_parent_color(tmp1, sibling, RB_BLACK);
			if (tmp2)
				rb_set_parent(tmp2, parent);
			__rb_rotate_set_parents(parent, sibling, root,
						RB_BLACK);
			augment_rotate(parent, sibling);
			break;
		}
	}
}

/* Non-inline version for rb_erase_augmented() use */
/*********************************************************************************************************
** 函数名称: __rb_erase_color
** 功能描述: 把指定的节点从指定的红黑树上移除之后用来处理树的平衡和节点颜色
** 输	 入: parent - 移除节点的父节点指针
**         : root - 指定的红黑树根节点指针
**         : augment_rotate - 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __rb_erase_color(struct rb_node *parent, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new))
{
	____rb_erase_color(parent, root, augment_rotate);
}
EXPORT_SYMBOL(__rb_erase_color);

/*
 * Non-augmented rbtree manipulation functions.
 *
 * We use dummy augmented callbacks here, and have the compiler optimize them
 * out of the rb_insert_color() and rb_erase() function definitions.
 */

static inline void dummy_propagate(struct rb_node *node, struct rb_node *stop) {}
static inline void dummy_copy(struct rb_node *old, struct rb_node *new) {}
static inline void dummy_rotate(struct rb_node *old, struct rb_node *new) {}

static const struct rb_augment_callbacks dummy_callbacks = {
	dummy_propagate, dummy_copy, dummy_rotate
};

/*********************************************************************************************************
** 函数名称: rb_insert_color
** 功能描述: 把指定的节点插入到指定的红黑树上之后用来处理树的平衡和节点颜色
** 输	 入: node - 指定的插入节点指针
**         : root - 指定的红黑树根节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void rb_insert_color(struct rb_node *node, struct rb_root *root)
{
	__rb_insert(node, root, dummy_rotate);
}
EXPORT_SYMBOL(rb_insert_color);

/*********************************************************************************************************
** 函数名称: rb_erase
** 功能描述: 把指定的节点从指定的红黑树上移除并处理树的平衡和节点颜色
** 输	 入: node - 指定的节点指针
**         : root - 指定的红黑树根节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void rb_erase(struct rb_node *node, struct rb_root *root)
{
	struct rb_node *rebalance;
	rebalance = __rb_erase_augmented(node, root, &dummy_callbacks);
	if (rebalance)
		____rb_erase_color(rebalance, root, dummy_rotate);
}
EXPORT_SYMBOL(rb_erase);

/*
 * Augmented rbtree manipulation functions.
 *
 * This instantiates the same __always_inline functions as in the non-augmented
 * case, but this time with user-defined callbacks.
 */
/*********************************************************************************************************
** 函数名称: __rb_insert_augmented
** 功能描述: 把指定的节点插入到指定的红黑树上之后用来处理树的平衡和节点颜色
** 输	 入: node - 指定的插入节点指针
**         : root - 指定的红黑树根节点指针
**         : augment_rotate - 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __rb_insert_augmented(struct rb_node *node, struct rb_root *root,
	void (*augment_rotate)(struct rb_node *old, struct rb_node *new))
{
	__rb_insert(node, root, augment_rotate);
}
EXPORT_SYMBOL(__rb_insert_augmented);

/*
 * This function returns the first node (in sort order) of the tree.
 */
/*********************************************************************************************************
** 函数名称: rb_first
** 功能描述: 返回指定红黑树上键值最小的节点指针
** 输	 入: root - 指定的红黑树根节点指针
** 输	 出: n - 键值最小的节点指针
**         : NULL - 指定的红黑树为空
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct rb_node *rb_first(const struct rb_root *root)
{
	struct rb_node	*n;

	n = root->rb_node;
	if (!n)
		return NULL;
	while (n->rb_left)
		n = n->rb_left;
	return n;
}
EXPORT_SYMBOL(rb_first);

/*********************************************************************************************************
** 函数名称: rb_last
** 功能描述: 返回指定红黑树上键值最大的节点指针
** 输	 入: root - 指定的红黑树根节点指针
** 输	 出: n - 键值最大的节点指针
**         : NULL - 指定的红黑树为空
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct rb_node *rb_last(const struct rb_root *root)
{
	struct rb_node	*n;

	n = root->rb_node;
	if (!n)
		return NULL;
	while (n->rb_right)
		n = n->rb_right;
	return n;
}
EXPORT_SYMBOL(rb_last);

/*********************************************************************************************************
** 函数名称: rb_next
** 功能描述: 返回指定节点所在红黑树上键值最接近的下一个节点指针
** 输	 入: root - 指定的节点指针
** 输	 出: parent - 键值最接近的下一个节点指针
**         : NULL - 指定的红黑树为空
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct rb_node *rb_next(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;

	/*
	 * If we have a right-hand child, go down and then left as far
	 * as we can.
	 */
	if (node->rb_right) {
		node = node->rb_right; 
		while (node->rb_left)
			node=node->rb_left;
		return (struct rb_node *)node;
	}

	/*
	 * No right-hand children. Everything down and left is smaller than us,
	 * so any 'next' node must be in the general direction of our parent.
	 * Go up the tree; any time the ancestor is a right-hand child of its
	 * parent, keep going up. First time it's a left-hand child of its
	 * parent, said parent is our 'next' node.
	 */
	while ((parent = rb_parent(node)) && node == parent->rb_right)
		node = parent;

	return parent;
}
EXPORT_SYMBOL(rb_next);

/*********************************************************************************************************
** 函数名称: rb_prev
** 功能描述: 返回指定节点所在红黑树上键值最接近的上一个节点指针
** 输	 入: root - 指定的节点指针
** 输	 出: parent - 键值最接近的上一个节点指针
**         : NULL - 指定的红黑树为空
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct rb_node *rb_prev(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;

	/*
	 * If we have a left-hand child, go down and then right as far
	 * as we can.
	 */
	if (node->rb_left) {
		node = node->rb_left; 
		while (node->rb_right)
			node=node->rb_right;
		return (struct rb_node *)node;
	}

	/*
	 * No left-hand children. Go up till we find an ancestor which
	 * is a right-hand child of its parent.
	 */
	while ((parent = rb_parent(node)) && node == parent->rb_left)
		node = parent;

	return parent;
}
EXPORT_SYMBOL(rb_prev);

/*********************************************************************************************************
** 函数名称: rb_replace_node
** 功能描述: 在指定的红黑树上用指定的新节点替换指定的旧节点
** 输	 入: victim - 指定的旧节点指针
**         : new - 指定的新节点指针
**         : root - 指定的红黑树根节点指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void rb_replace_node(struct rb_node *victim, struct rb_node *new,
		     struct rb_root *root)
{
	struct rb_node *parent = rb_parent(victim);

	/* Set the surrounding nodes to point to the replacement */
	__rb_change_child(victim, new, parent, root);
	if (victim->rb_left)
		rb_set_parent(victim->rb_left, new);
	if (victim->rb_right)
		rb_set_parent(victim->rb_right, new);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;
}
EXPORT_SYMBOL(rb_replace_node);

/*********************************************************************************************************
** 函数名称: rb_left_deepest_node
** 功能描述: 按照左序优先的方式在红黑树上查找和指定节点之间深度相差最大的节点
** 输	 入: node - 指定的节点指针
** 输	 出: node - 和指定节点深度相差最大的节点指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct rb_node *rb_left_deepest_node(const struct rb_node *node)
{
	for (;;) {
		if (node->rb_left)
			node = node->rb_left;
		else if (node->rb_right)
			node = node->rb_right;
		else
			return (struct rb_node *)node;
	}
}

/*********************************************************************************************************
** 函数名称: rb_next_postorder
** 功能描述: 返回指定节点所在红黑树上键值最接近的下一个节点指针
** 输	 入: node - 指定的节点指针
** 输	 出: parent - 和指定节点深度相差最大的节点指针
**         : NULL - 没找到匹配的节点
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct rb_node *rb_next_postorder(const struct rb_node *node)
{
	const struct rb_node *parent;
	if (!node)
		return NULL;
	parent = rb_parent(node);

	/* If we're sitting on node, we've already seen our children */
	if (parent && node == parent->rb_left && parent->rb_right) {
		/* If we are the parent's left node, go to the parent's right
		 * node then all the way down to the left */
		return rb_left_deepest_node(parent->rb_right);
	} else
		/* Otherwise we are the parent's right node, and the parent
		 * should be next */
		return (struct rb_node *)parent;
}
EXPORT_SYMBOL(rb_next_postorder);

/*********************************************************************************************************
** 函数名称: rb_first_postorder
** 功能描述: 返回指定红黑树上键值最小的节点指针
** 输	 入: root - 指定红黑树的根节点指针
** 输	 出: rb_node * - 键值最小的节点指针
**         : NULL - 没找到匹配的节点
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct rb_node *rb_first_postorder(const struct rb_root *root)
{
	if (!root->rb_node)
		return NULL;

	return rb_left_deepest_node(root->rb_node);
}
EXPORT_SYMBOL(rb_first_postorder);
