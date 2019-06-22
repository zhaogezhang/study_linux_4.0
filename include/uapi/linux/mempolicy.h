/*
 * NUMA memory policies for Linux.
 * Copyright 2003,2004 Andi Kleen SuSE Labs
 */
#ifndef _UAPI_LINUX_MEMPOLICY_H
#define _UAPI_LINUX_MEMPOLICY_H

#include <linux/errno.h>


/*
 * Both the MPOL_* mempolicy mode and the MPOL_F_* optional mode flags are
 * passed by the user to either set_mempolicy() or mbind() in an 'int' actual.
 * The MPOL_MODE_FLAGS macro determines the legal set of optional mode flags.
 */
/* Policies */
// 参考文档：Documentation\vm\numa_memory_policy.txt
// 内存管理系统支持的内存分配策略
enum {
	// 回退到下一个指定的内存分配策略，例如：
	// 1. default task policy --> system default policy
	// 2. default vma policy  --> task policy
	MPOL_DEFAULT,

	// 在分配内存的时候，会从这个内存分配策略指定的一个内存节点
	// 中分配，如果分配失败，则会以这个节点内存距离为基础，按顺序
	// 增加节点距离来寻找距离最短的内存节点来分配内存
	MPOL_PREFERRED,

	// 在分配内存的时候，会从这个内存分配策略指定的一些内存节点
	// 中分配，并且会在这些限定的节点中，选择最进的节点分配内存
	MPOL_BIND,

	// 在分配内存的时候，会从这个内存分配策略指定的一些内存节点
	// 中分配，具体选择哪个内存节点和上下文有关，具体如下：
	// 1. 如果是分配匿名页面，则选择发生 page fault 内存页在 segment 中
	//    的偏移量对可选节点个数求模，得出的余数就是我们选择节点的索引值
	// 2. 如果是文件映射内存分配，则会以当前 task 维护的一个计数值为索引
	//    进行内存分配，这个计数值会在分配内存后、在合法的范围内循环递增
	MPOL_INTERLEAVE,

	// 从当前 cpu 所在的内存节点上分配内存，即距离最近的内存节点
	MPOL_LOCAL,
	MPOL_MAX,	/* always last member of enum */
};

enum mpol_rebind_step {
	MPOL_REBIND_ONCE,	/* do rebind work at once(not by two step) */
	MPOL_REBIND_STEP1,	/* first step(set all the newly nodes) */
	MPOL_REBIND_STEP2,	/* second step(clean all the disallowed nodes)*/
	MPOL_REBIND_NSTEP,
};

/* Flags for set_mempolicy */
// 这个内存分配策略标志变量表示，由用户传进来的 user nodemask 信息是不可
// 以被重新映射到 task or VMA's set of allowed nodes，我们在分配内存的时候
// 会从 (user nodemask & (task or VMA's set of allowed nodes)) 中选合适的
// 内存节点来分配内存，如果在这些指定的内存节点上无法成功分配内存，则会
// 使用 default policy
// note：task or VMA's set of allowed nodes 在系统运行中可能会改变
#define MPOL_F_STATIC_NODES	(1 << 15)

// 这个内存分配策略标志变量表示，由用户传进来的 user nodemask 信息是可以
// 被重新映射到 task or VMA's set of allowed nodes，用户传进来的 user nodemask 
// 信息表示的是在 task or VMA's set of allowed nodes 中的索引值，换句话说就是：
// 如果 user nodemask = 0, 2, and 4，那么我们将选择 task or VMA's set of allowed 
// nodes 中的第一个、第三个和第五个内存节点来分配内存
#define MPOL_F_RELATIVE_NODES	(1 << 14)

/*
 * MPOL_MODE_FLAGS is the union of all possible optional mode flags passed to
 * either set_mempolicy() or mbind().
 */
#define MPOL_MODE_FLAGS	(MPOL_F_STATIC_NODES | MPOL_F_RELATIVE_NODES)

/* Flags for get_mempolicy */
#define MPOL_F_NODE	(1<<0)	/* return next IL mode instead of node mask */
#define MPOL_F_ADDR	(1<<1)	/* look up vma using address */
#define MPOL_F_MEMS_ALLOWED (1<<2) /* return allowed memories */

/* Flags for mbind */
#define MPOL_MF_STRICT	(1<<0)	/* Verify existing pages in the mapping */
#define MPOL_MF_MOVE	 (1<<1)	/* Move pages owned by this process to conform
				   to policy */
#define MPOL_MF_MOVE_ALL (1<<2)	/* Move every page to conform to policy */
#define MPOL_MF_LAZY	 (1<<3)	/* Modifies '_MOVE:  lazy migrate on fault */
#define MPOL_MF_INTERNAL (1<<4)	/* Internal flags start here */

#define MPOL_MF_VALID	(MPOL_MF_STRICT   | 	\
			 MPOL_MF_MOVE     | 	\
			 MPOL_MF_MOVE_ALL)

/*
 * Internal flags that share the struct mempolicy flags word with
 * "mode flags".  These flags are allocated from bit 0 up, as they
 * are never OR'ed into the mode in mempolicy API arguments.
 */
#define MPOL_F_SHARED  (1 << 0)	/* identify shared policies */
#define MPOL_F_LOCAL   (1 << 1)	/* preferred local allocation */
#define MPOL_F_REBINDING (1 << 2)	/* identify policies in rebinding */
#define MPOL_F_MOF	(1 << 3) /* this policy wants migrate on fault */
#define MPOL_F_MORON	(1 << 4) /* Migrate On protnone Reference On Node */


#endif /* _UAPI_LINUX_MEMPOLICY_H */
