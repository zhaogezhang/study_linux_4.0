#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H

/* 表示在用位图对系统 node 进行标志时需要的 bit 位数 */
#ifdef CONFIG_NODES_SHIFT
#define NODES_SHIFT     CONFIG_NODES_SHIFT
#else
#define NODES_SHIFT     0
#endif

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#define	NUMA_NO_NODE	(-1)  /* 无效的 node id */

#endif /* _LINUX_NUMA_H */
