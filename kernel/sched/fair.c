/*
 * Completely Fair Scheduling (CFS) Class (SCHED_NORMAL/SCHED_BATCH)
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuidle.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>

#include <trace/events/sched.h>

#include "sched.h"

/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 6ms * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * NOTE: this latency value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS are of variable length
 * and have no persistent notion like in traditional, time-slice
 * based scheduling concepts.
 *
 * (to see the precise effective timeslice length of your workload,
 *  run vmstat and monitor the context-switches (cs) field)
 */
/* 表示 cpu 消耗性任务的最大抢占调度延迟时间，默认为 6ms * (1 + ilog(ncpus)) */
unsigned int sysctl_sched_latency = 6000000ULL;  /* 6ms */

unsigned int normalized_sysctl_sched_latency = 6000000ULL;

/*
 * The initial- and re-scaling of tunables is configurable
 * (default SCHED_TUNABLESCALING_LOG = *(1+ilog(ncpus))
 *
 * Options are:
 * SCHED_TUNABLESCALING_NONE - unscaled, always *1
 * SCHED_TUNABLESCALING_LOG - scaled logarithmical, *1+ilog(ncpus)
 * SCHED_TUNABLESCALING_LINEAR - scaled linear, *ncpus
 */
/* 表示当前系统使用的调度粒度调整策略 */
enum sched_tunable_scaling sysctl_sched_tunable_scaling
	= SCHED_TUNABLESCALING_LOG;

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 0.75 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
/* 表示 cpu 消耗型任务的最小抢占粒度，默认为 0.75ms * (1 + ilog(ncpus)) */
unsigned int sysctl_sched_min_granularity = 750000ULL;

unsigned int normalized_sysctl_sched_min_granularity = 750000ULL;

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 8;

/*
 * After fork, child runs first. If set to 0 (default) then
 * parent will (try to) run first.
 */
/* 该变量表示在创建子进程的时候是否让子进程抢占父进程，即使父进程的
   vruntime 小于子进程，这个会减少公平性，但是可以降低 write_on_copy
   具体要根据系统的应用情况来考量使用哪种方式（见 task_fork_fair 过程）*/
unsigned int sysctl_sched_child_runs_first __read_mostly;

/*
 * SCHED_OTHER wake-up granularity.
 * (default: 1 msec * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
/* 该变量表示进程被唤醒后至少应该运行的时间的基数，它只是用来判断某个进程
   是否应该抢占当前进程，并不代表它能够执行的最小时间（sysctl_sched_min_granularity）
   如果这个数值越小，那么发生抢占的概率也就越高（见 wakeup_gran、wakeup_preempt_entity 函数）*/
unsigned int sysctl_sched_wakeup_granularity = 1000000UL;

unsigned int normalized_sysctl_sched_wakeup_granularity = 1000000UL;

/* 该变量用来判断一个进程是否还是 hot，如果进程的运行时间(now - p->se.exec_start)
   小于它，那么内核认为它的 code 还在 cache 里，所以该进程还是 hot，那么在迁移的
   时候就不会考虑它 */
const_debug unsigned int sysctl_sched_migration_cost = 500000UL;

/*
 * The exponential sliding  window over which load is averaged for shares
 * distribution.
 * (default: 10msec)
 */
unsigned int __read_mostly sysctl_sched_shares_window = 10000000UL;

#ifdef CONFIG_CFS_BANDWIDTH
/*
 * Amount of runtime to allocate from global (tg) to local (per-cfs_rq) pool
 * each time a cfs_rq requests quota.
 *
 * Note: in the case that the slice exceeds the runtime remaining (either due
 * to consumption or the quota being specified to be smaller than the slice)
 * we will always only issue the remaining available time.
 *
 * default: 5 msec, units: microseconds
  */
unsigned int sysctl_sched_cfs_bandwidth_slice = 5000UL;
#endif

/*********************************************************************************************************
** 函数名称: update_load_add
** 功能描述: 对指定的负载权重执行指定的增量更新
** 输	 入: lw - 指定的负载权重指针
**         : inc - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
	lw->weight += inc;
	lw->inv_weight = 0;
}

/*********************************************************************************************************
** 函数名称: update_load_sub
** 功能描述: 对指定的负载权重执行指定的减量更新
** 输	 入: lw - 指定的负载权重指针
**         : inc - 指定的减量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_load_sub(struct load_weight *lw, unsigned long dec)
{
	lw->weight -= dec;
	lw->inv_weight = 0;
}

/*********************************************************************************************************
** 函数名称: update_load_set
** 功能描述: 设置指定的负载权重为指定的值
** 输	 入: lw - 指定的负载权重指针
**         : w - 指定的负载权重值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_load_set(struct load_weight *lw, unsigned long w)
{
	lw->weight = w;
	lw->inv_weight = 0;
}

/*
 * Increase the granularity value when there are more CPUs,
 * because with more CPUs the 'effective latency' as visible
 * to users decreases. But the relationship is not linear,
 * so pick a second-best guess by going with the log2 of the
 * number of CPUs.
 *
 * 当有更多的 cpu 时，增加粒度值，因为 cpu 越多，用户可见的“有效延迟”
 * 就越小。但是这个关系不是线性的，所以根据 cpu 数量的 log2 进行猜测
 * 也是一个比较好的办法
 *
 * This idea comes from the SD scheduler of Con Kolivas:
 */
/*********************************************************************************************************
** 函数名称: get_update_sysctl_factor
** 功能描述: 获取当前系统使用的调度粒度调整策略获取系统控制系数值
** 输	 入: 
** 输	 出: factor - 当前系统控制系数值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int get_update_sysctl_factor(void)
{
	unsigned int cpus = min_t(int, num_online_cpus(), 8);
	unsigned int factor;

	switch (sysctl_sched_tunable_scaling) {
	case SCHED_TUNABLESCALING_NONE:
		factor = 1;
		break;
	case SCHED_TUNABLESCALING_LINEAR:
		factor = cpus;
		break;
	case SCHED_TUNABLESCALING_LOG:
	default:
		factor = 1 + ilog2(cpus);
		break;
	}

	return factor;
}

/*********************************************************************************************************
** 函数名称: update_sysctl
** 功能描述: 根据当前系统使用的调度粒度调整策略更新和系统控制相关的调度参数
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_sysctl(void)
{
	unsigned int factor = get_update_sysctl_factor();

#define SET_SYSCTL(name) \
	(sysctl_##name = (factor) * normalized_sysctl_##name)

    /* 根据获取到的系统控制系数值更新当前系统控制相关的调度参数 */
	SET_SYSCTL(sched_min_granularity);
	SET_SYSCTL(sched_latency);
	SET_SYSCTL(sched_wakeup_granularity);
#undef SET_SYSCTL
}

/*********************************************************************************************************
** 函数名称: sched_init_granularity
** 功能描述: 初始化和当前系统控制相关的调度参数
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void sched_init_granularity(void)
{
	update_sysctl();
}

#define WMULT_CONST	(~0U)
#define WMULT_SHIFT	32

/*********************************************************************************************************
** 函数名称: __update_inv_weight
** 功能描述: 根据指定的负载权重的 lw->weight 值更新 lw->inv_weight 字段值
** 输	 入: lw - 指定的负载权重指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __update_inv_weight(struct load_weight *lw)
{
	unsigned long w;

	if (likely(lw->inv_weight))
		return;
    
	/* 获取和指定的负载权重对应的值 */
	w = scale_load_down(lw->weight);

	if (BITS_PER_LONG > 32 && unlikely(w >= WMULT_CONST))
		lw->inv_weight = 1;
	else if (unlikely(!w))
		lw->inv_weight = WMULT_CONST;
	else
		lw->inv_weight = WMULT_CONST / w; /* lw->weight * lw->inv_weight = 2^32 = 0x80000000 */
}

/*
 * delta_exec * weight / lw.weight
 *   OR
 * (delta_exec * (weight * lw->inv_weight)) >> WMULT_SHIFT
 *
 * Either weight := NICE_0_LOAD and lw \e prio_to_wmult[], in which case
 * we're guaranteed shift stays positive because inv_weight is guaranteed to
 * fit 32 bits, and NICE_0_LOAD gives another 10 bits; therefore shift >= 22.
 *
 * Or, weight =< lw.weight (because lw.weight is the runqueue weight), thus
 * weight/lw.weight <= 1, and therefore our shift will also be positive. 
 */
/*********************************************************************************************************
** 函数名称: __calc_delta
** 功能描述: 计算指定权重的 cpu 物理运行时间在指定的负载权重下对应的物理/虚拟运行时间
** 注     释: 1.    delta_exec * weight / lw.weight
**         :    = delta_exec * weight / (2^32 / lw->inv_weight)
**         :    = delta_exec * weight * lw->inv_weight / 2^32
**         : 2. 当 weight = NICE_0_LOAD 时，计算得出的是虚拟运行时间
** 输	 入: delta_exec - 指定调度实例的 cpu 物理运行时间
**         : weight - 指定的权重参数
**         : lw - 指定的负载权重参数
** 输	 出: u64 - 对应的 cpu 物理运行时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 __calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw)
{
	/* 获取和指定的负载权重对应的值 */
	u64 fact = scale_load_down(weight);
	
	int shift = WMULT_SHIFT;

	/* lw->weight * lw->inv_weight = 2^32 = 0x80000000 */
	__update_inv_weight(lw);

	if (unlikely(fact >> 32)) {
		while (fact >> 32) {
			fact >>= 1;
			shift--;
		}
	}

	/* hint to use a 32x32->64 mul */
	fact = (u64)(u32)fact * lw->inv_weight;

	while (fact >> 32) {
		fact >>= 1;
		shift--;
	}

    /* 将指定的 u64 和指定的 u32 相乘后右移指定的位数 */
	return mul_u64_u32_shr(delta_exec, fact, shift);
}


const struct sched_class fair_sched_class;

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

#ifdef CONFIG_FAIR_GROUP_SCHED
/* cpu runqueue to which this cfs_rq is attached */
/*********************************************************************************************************
** 函数名称: rq_of
** 功能描述: 获取指定的 cfs 运行队列所属的 cpu 运行队列指针
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: cfs_rq->rq - cpu 的运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* An entity is a task if it doesn't "own" a runqueue */
/*********************************************************************************************************
** 函数名称: entity_is_task
** 功能描述: 判断指定的调度实例是否是一个任务
** 输	 入: se - 指定的调度实例指针
** 输	 出: 1 - 是一个线程
**         : 0 - 是一个调度组
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define entity_is_task(se)	(!se->my_q)

/*********************************************************************************************************
** 函数名称: task_of
** 功能描述: 获取指定的调度实体所在的 task_struct 结构指针
** 输	 入: se - 指定的调度实体指针
** 输	 出: task_struct * - 获取到的 task_struct 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct task_struct *task_of(struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	WARN_ON_ONCE(!entity_is_task(se));
#endif
	return container_of(se, struct task_struct, se);
}

/* Walk up scheduling entities hierarchy */
/*********************************************************************************************************
** 函数名称: for_each_sched_entity
** 功能描述: 遍历指定的任务组调度实例到“根任务组”之间遍历路径的每一个任务组调度实例
**         : 因为“根任务组”的 root_task_group->se[cpu_id] = NULL，所以 root_task_group 的所有
**         : 子任务组调度实例的 child_se->parent = NULL，详情见 init_tg_cfs_entry 函数
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: se - 遍历过程中可操作的临时变量指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)

/*********************************************************************************************************
** 函数名称: task_cfs_rq
** 功能描述: 获取指定的调度实例所在的 cfs 运行队列指针
** 输	 入: p - 指定的调度实例指针
** 输	 出: p->se.cfs_rq - 获取到的 cfs 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return p->se.cfs_rq;
}

/* runqueue on which this entity is (to be) queued */
/*********************************************************************************************************
** 函数名称: cfs_rq_of
** 功能描述: 获取指定的调度实例所在的 cfs 运行队列指针
** 输	 入: se - 指定的调度实例指针
** 输	 出: se->cfs_rq - 获取到的 cfs 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	return se->cfs_rq;
}

/* runqueue "owned" by this group */
/*********************************************************************************************************
** 函数名称: group_cfs_rq
** 功能描述: 获取指定的任务组拥有的运行队列指针
** 注     释: 如果当前调度实例代表的是一个任务组，则指向当前任务组拥有的运行队列，如果当前调度实例
**         : 代表的是一个线程，则指向 NULL
** 输	 入: grp - 指定的任务组指针
** 输	 出: grp->my_q - 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return grp->my_q;
}

static void update_cfs_rq_blocked_load(struct cfs_rq *cfs_rq,
				       int force_update);

/*********************************************************************************************************
** 函数名称: list_add_leaf_cfs_rq
** 功能描述: 把指定的 cfs 运行队列添加到其所属的 cpu 运行队列上，并且需要保证我们添加的新 cfs 运行队列
**         : 在链表上的位置要在其父任务组节点的前面，详情见 enqueue_entity 函数
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void list_add_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
	if (!cfs_rq->on_list) {
		/*
		 * Ensure we either appear before our parent (if already
		 * enqueued) or force our parent to appear after us when it is
		 * enqueued.  The fact that we always enqueue bottom-up
		 * reduces this to two cases.
		 */
		/* 把指定的任务组的 cfs 运行队列添加到所属父节点任务组的 cfs 运行队列的前面 */
		if (cfs_rq->tg->parent &&
		    cfs_rq->tg->parent->cfs_rq[cpu_of(rq_of(cfs_rq))]->on_list) {
			list_add_rcu(&cfs_rq->leaf_cfs_rq_list,
				&rq_of(cfs_rq)->leaf_cfs_rq_list);
		} else {
			list_add_tail_rcu(&cfs_rq->leaf_cfs_rq_list,
				&rq_of(cfs_rq)->leaf_cfs_rq_list);
		}

		cfs_rq->on_list = 1;
		/* We should have no load, but we need to update last_decay. */
		update_cfs_rq_blocked_load(cfs_rq, 0);
	}
}

/*********************************************************************************************************
** 函数名称: list_del_leaf_cfs_rq
** 功能描述: 把指定的 cfs 运行队列从其所属的 cpu 运行队列上移除
** 注     释: 这个函数是在任务组功能中调用的
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void list_del_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
	if (cfs_rq->on_list) {
		list_del_rcu(&cfs_rq->leaf_cfs_rq_list);
		cfs_rq->on_list = 0;
	}
}

/* Iterate thr' all leaf cfs_rq's on a runqueue */
/*********************************************************************************************************
** 函数名称: for_each_leaf_cfs_rq
** 功能描述: 遍历指定的 cpu 运行队列的所有任务组叶子节点的 cfs 运行队列（因为调度组有自己的 cfs 运行
**         : 队列，且一个 cpu 运行队列上可能包含多个 cfs 运行队列）
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : cfs_rq - 遍历时使用的临时变量指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
	list_for_each_entry_rcu(cfs_rq, &rq->leaf_cfs_rq_list, leaf_cfs_rq_list)

/* Do the two (enqueued) entities belong to the same group ? */
/*********************************************************************************************************
** 函数名称: is_same_group
** 功能描述: 判断指定的两个调度实例是否在同一个调度组中
** 输	 入: se - 指定的第一个调度实例指针
**         : pse - 指定的第二个调度实例指针
** 输	 出: se->cfs_rq - 他们共同所属调度组的 cfs 运行队列（每 cpu 类型变量）指针
**         : NULL - 不属于同一个调度组
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	if (se->cfs_rq == pse->cfs_rq)
		return se->cfs_rq;

	return NULL;
}

/*********************************************************************************************************
** 函数名称: parent_entity
** 功能描述: 获取指定的任务组调度实例的父调度实例节点指针
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: se->parent - 父调度实例节点指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return se->parent;
}

/*********************************************************************************************************
** 函数名称: find_matching_se
** 功能描述: 获取指定的两个任务组调度实例在任务组树形结构上深度相同且同属一个任务组的调度实例指针
** 输	 入: se - 指定的第一个任务组调度实例指针
**         : pse - 指定的第二个任务组调度实例指针
** 输	 出: *se - 第一个任务组调度实例节点指针
**         : *pse - 第二个任务组调度实例节点指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
find_matching_se(struct sched_entity **se, struct sched_entity **pse)
{
	int se_depth, pse_depth;

	/*
	 * preemption test can be made between sibling entities who are in the
	 * same cfs_rq i.e who have a common parent. Walk up the hierarchy of
	 * both tasks until we find their ancestors who are siblings of common
	 * parent.
	 */

	/* First walk up until both entities are at same depth */
	se_depth = (*se)->depth;
	pse_depth = (*pse)->depth;

    /* 让指定的两个调度任务组实例在任务组树形结构上的深度相同 */
	while (se_depth > pse_depth) {
		se_depth--;
		*se = parent_entity(*se);
	}

	while (pse_depth > se_depth) {
		pse_depth--;
		*pse = parent_entity(*pse);
	}

    /* 在两个调度任务组实例在任务组树形结构上的深度相同时，获取他们的父节点调度任务组实例指针 */
	while (!is_same_group(*se, *pse)) {
		*se = parent_entity(*se);
		*pse = parent_entity(*pse);
	}
}

#else	/* !CONFIG_FAIR_GROUP_SCHED */

/*********************************************************************************************************
** 函数名称: entity_is_task
** 功能描述: 判断指定的调度实例是否是一个线程
** 输	 入: se - 指定的调度实例指针
** 输	 出: 1 - 是一个线程
**         : 0 - 是一个调度组
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

/*********************************************************************************************************
** 函数名称: rq_of
** 功能描述: 获取指定的 cfs 运行队列所属的 cpu 运行队列指针
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: cfs_rq->rq - cpu 的运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

/*********************************************************************************************************
** 函数名称: entity_is_task
** 功能描述: 判断指定的调度实例是否是一个任务
** 输	 入: se - 指定的调度实例指针
** 输	 出: 1 - 是一个线程
**         : 0 - 是一个调度组
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define entity_is_task(se)	1

/*********************************************************************************************************
** 函数名称: for_each_sched_entity
** 功能描述: 遍历指定的任务组调度实例到“根任务组”之间遍历路径的每一个任务组调度实例
**         : 因为“根任务组”的 root_task_group->se[cpu_id] = NULL，所以 root_task_group 的所有
**         : 子任务组调度实例的 child_se->parent = NULL，详情见 init_tg_cfs_entry 函数
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: se - 遍历过程中可操作的临时变量指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_sched_entity(se) \
		for (; se; se = NULL)

/*********************************************************************************************************
** 函数名称: task_cfs_rq
** 功能描述: 获取指定的调度实例所在的 cfs 运行队列指针
** 输	 入: p - 指定的调度实例指针
** 输	 出: p->se.cfs_rq - 获取到的 cfs 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return &task_rq(p)->cfs;
}

/*********************************************************************************************************
** 函数名称: cfs_rq_of
** 功能描述: 获取指定的调度实例所在的 cfs 运行队列指针
** 输	 入: se - 指定的调度实例指针
** 输	 出: se->cfs_rq - 获取到的 cfs 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->cfs;
}

/* runqueue "owned" by this group */
/*********************************************************************************************************
** 函数名称: group_cfs_rq
** 功能描述: 获取指定的任务组拥有的运行队列指针
** 注     释: 如果当前调度实例代表的是一个任务组，则指向当前任务组拥有的运行队列，如果当前调度实例
**         : 代表的是一个线程，则指向 NULL
** 输	 入: grp - 指定的任务组指针
** 输	 出: grp->my_q - 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return NULL;
}

/*********************************************************************************************************
** 函数名称: list_add_leaf_cfs_rq
** 功能描述: 把指定的 cfs 运行队列添加到其所属的 cpu 运行队列上，并且需要保证我们添加的新 cfs 运行队列
**         : 在链表上的位置要在其父任务组节点的前面，详情见 enqueue_entity 函数
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void list_add_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
}

/*********************************************************************************************************
** 函数名称: list_del_leaf_cfs_rq
** 功能描述: 把指定的 cfs 运行队列从其所属的 cpu 运行队列上移除
** 注     释: 这个函数是在任务组功能中调用的
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void list_del_leaf_cfs_rq(struct cfs_rq *cfs_rq)
{
}

/*********************************************************************************************************
** 函数名称: for_each_leaf_cfs_rq
** 功能描述: 遍历指定的 cpu 运行队列的所有的 cfs 运行队列（因为调度组有自己的 cfs 运行队列，所以一个
**         : cpu 运行队列上可能包含多个 cfs 运行队列）
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : cfs_rq - 遍历时使用的临时变量指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
		for (cfs_rq = &rq->cfs; cfs_rq; cfs_rq = NULL)

/*********************************************************************************************************
** 函数名称: parent_entity
** 功能描述: 获取指定的调度任务组实例的父节点指针
** 输	 入: se - 指定的调度任务组实例指针
** 输	 出: se->parent - 父节点指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return NULL;
}

/*********************************************************************************************************
** 函数名称: find_matching_se
** 功能描述: 获取指定的两个调度任务组实例在任务组树形结构上相同深度的父节点调度任务组实例指针
** 输	 入: se - 指定的第一个调度任务组实例指针
**         : pse - 指定的第二个调度任务组实例指针
** 输	 出: *se - 第一个调度任务组实例的父节点指针
**         : *pse - 第二个调度任务组实例的父节点指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
find_matching_se(struct sched_entity **se, struct sched_entity **pse)
{
}

#endif	/* CONFIG_FAIR_GROUP_SCHED */

static __always_inline
void account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec);

/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

/*********************************************************************************************************
** 函数名称: max_vruntime
** 功能描述: 获取指定的两个虚拟时间中的较大者
** 输	 入: max_vruntime - 指定的第一个虚拟时间
**         : vruntime - 指定的第二个虚拟时间
** 输	 出: max_vruntime - 较大的虚拟时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);
	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

/*********************************************************************************************************
** 函数名称: min_vruntime
** 功能描述: 获取指定的两个虚拟时间中的较小者
** 输	 入: min_vruntime - 指定的第一个虚拟时间
**         : vruntime - 指定的第二个虚拟时间
** 输	 出: min_vruntime - 较小的虚拟时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

/*********************************************************************************************************
** 函数名称: entity_before
** 功能描述: 判断指定的第一个调度实例的虚拟时间是否小于指定的第二个调度实例的虚拟时间
** 输	 入: a - 指定的第一个调度实例指针
**         : b - 指定的第二个调度实例指针
** 输	 出: 1 - 小于
**         : 0 - 不小于
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int entity_before(struct sched_entity *a,
				struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

/*********************************************************************************************************
** 函数名称: update_min_vruntime
** 功能描述: 更新指定的 cfs 运行队列的 min_vruntime 成员值，可能的值为 cfs_rq->min_vruntime
**         : cfs_rq->curr->vruntime 和 cfs_rq->rb_leftmost->vruntime
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
	u64 vruntime = cfs_rq->min_vruntime;

	if (cfs_rq->curr)
		vruntime = cfs_rq->curr->vruntime;

	if (cfs_rq->rb_leftmost) {
		struct sched_entity *se = rb_entry(cfs_rq->rb_leftmost,
						   struct sched_entity,
						   run_node);

		if (!cfs_rq->curr)
			vruntime = se->vruntime;
		else
			vruntime = min_vruntime(vruntime, se->vruntime);
	}

	/* ensure we never gain time by being placed backwards. */
	cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
#ifndef CONFIG_64BIT
	smp_wmb();
	cfs_rq->min_vruntime_copy = cfs_rq->min_vruntime;
#endif
}

/*
 * Enqueue an entity into the rb-tree:
 */
/*********************************************************************************************************
** 函数名称: __enqueue_entity
** 功能描述: 把指定的调度实例以虚拟运行时间为键值添加到指定的 cfs 运行队列的红黑树上
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct rb_node **link = &cfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	int leftmost = 1;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, run_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (entity_before(se, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries (it is frequently
	 * used):
	 */
	if (leftmost)
		cfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &cfs_rq->tasks_timeline);
}

/*********************************************************************************************************
** 函数名称: __dequeue_entity
** 功能描述: 把指定的调度实例从指定的 cfs 运行队列的红黑树上移除
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->rb_leftmost == &se->run_node) {
		struct rb_node *next_node;

		next_node = rb_next(&se->run_node);
		cfs_rq->rb_leftmost = next_node;
	}

	rb_erase(&se->run_node, &cfs_rq->tasks_timeline);
}

/*********************************************************************************************************
** 函数名称: __pick_first_entity
** 功能描述: 获取指定的 cfs 运行队列 cfs_rq->rb_leftmost 指向的调度实例指针
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: sched_entity * - 调度实例指针
**         : NULL - 获取失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *left = cfs_rq->rb_leftmost;

	if (!left)
		return NULL;

	return rb_entry(left, struct sched_entity, run_node);
}

/*********************************************************************************************************
** 函数名称: __pick_next_entity
** 功能描述: 获取和指定的调度实例的虚拟时间最接近的下一个调度实例指针
** 输	 入: se - 指定的调度实例指针
** 输	 出: sched_entity * - 获取的调度实例指针
**         : NULL - 获取失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct sched_entity *__pick_next_entity(struct sched_entity *se)
{
	struct rb_node *next = rb_next(&se->run_node);

	if (!next)
		return NULL;

	return rb_entry(next, struct sched_entity, run_node);
}

#ifdef CONFIG_SCHED_DEBUG
/*********************************************************************************************************
** 函数名称: __pick_next_entity
** 功能描述: 获取指定的 cfs 运行队列中虚拟时间最大的调度实例指针
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: sched_entity * - 获取的调度实例指针
**         : NULL - 获取失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct sched_entity *__pick_last_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *last = rb_last(&cfs_rq->tasks_timeline);

	if (!last)
		return NULL;

	return rb_entry(last, struct sched_entity, run_node);
}

/**************************************************************
 * Scheduling class statistics methods:
 */

int sched_proc_update_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	int factor = get_update_sysctl_factor();

	if (ret || !write)
		return ret;

	sched_nr_latency = DIV_ROUND_UP(sysctl_sched_latency,
					sysctl_sched_min_granularity);

#define WRT_SYSCTL(name) \
	(normalized_sysctl_##name = sysctl_##name / (factor))
	WRT_SYSCTL(sched_min_granularity);
	WRT_SYSCTL(sched_latency);
	WRT_SYSCTL(sched_wakeup_granularity);
#undef WRT_SYSCTL

	return 0;
}
#endif

/*
 * delta /= w
 */
/*********************************************************************************************************
** 函数名称: calc_delta_fair
** 功能描述: 计算指定的调度实例指定的 cpu 物理运行时间对应的虚拟运行时间，根据计算公式可以得出：
**         : 当调度实例 se->load.weight 越小，物理运行时间对应的虚拟时间越大，即优先级越低
** 输	 入: delta - 指定的 cpu 物理运行时间
**         : se - 指定的调度实例指针
** 输	 出: delta - 对应的虚拟运行时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 calc_delta_fair(u64 delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &se->load); /* delta_exec * (NICE_0_LOAD / se->load.weight) */

	return delta;
}

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
/*********************************************************************************************************
** 函数名称: __sched_period
** 功能描述: 根据当前正在运行的调度实例个数计算每个任务运行一次需要的物理时间周期
** 输	 入: nr_running - 当前正在运行的调度实例个数
** 输	 出: period - 每个任务运行一次需要的时间周期，单位 ns
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 __sched_period(unsigned long nr_running)
{
	u64 period = sysctl_sched_latency;
	unsigned long nr_latency = sched_nr_latency;

	if (unlikely(nr_running > nr_latency)) {
		period = sysctl_sched_min_granularity;
		period *= nr_running;
	}

	return period;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*P[w/rw]
 */
/*********************************************************************************************************
** 函数名称: sched_slice
** 功能描述: 计算指定的 cfs 运行队列上指定的调度实例（调度任务组实例）可以分配到的 cpu 物理运行
**         : 时间片，单位 ns
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例（调度任务组实例）指针
** 输	 出: slice - 指定调度实例可分配到的 cpu 物理运行时间，单位 ns
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u64 slice = __sched_period(cfs_rq->nr_running + !se->on_rq);

	for_each_sched_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		cfs_rq = cfs_rq_of(se);
		load = &cfs_rq->load;

		if (unlikely(!se->on_rq)) {
			lw = cfs_rq->load;

			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = __calc_delta(slice, se->load.weight, load);
	}
	return slice;
}

/*
 * We calculate the vruntime slice of a to-be-inserted task.
 *
 * vs = s/w
 */
/*********************************************************************************************************
** 函数名称: sched_vslice
** 功能描述: 计算指定的 cfs 运行队列上指定的调度实例（调度任务组实例）可以分配到的虚拟运行时间
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例（调度任务组实例）指针
** 输	 出: u64 - 指定调度实例可分配到的虚拟运行时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 sched_vslice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	return calc_delta_fair(sched_slice(cfs_rq, se), se);
}

#ifdef CONFIG_SMP
static int select_idle_sibling(struct task_struct *p, int cpu);
static unsigned long task_h_load(struct task_struct *p);

static inline void __update_task_entity_contrib(struct sched_entity *se);

/* Give new task start runnable values to heavy its load in infant time */
/*********************************************************************************************************
** 函数名称: init_task_runnable_average
** 功能描述: 初始化指定任务的系统负载相关的数据结构信息
** 输	 入: p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void init_task_runnable_average(struct task_struct *p)
{
	u32 slice;

	slice = sched_slice(task_cfs_rq(p), &p->se) >> 10;
	p->se.avg.runnable_avg_sum = slice;
	p->se.avg.runnable_avg_period = slice;
	__update_task_entity_contrib(&p->se);
}
#else
void init_task_runnable_average(struct task_struct *p)
{
}
#endif

/*
 * Update the current task's runtime statistics.
 */
/*********************************************************************************************************
** 函数名称: update_curr
** 功能描述: 更新指定的 cfs 运行队列中当前正在运行的调度实例的运行时统计信息，操作如下：
**         : 1. 更新当前正在运行的调度实例的物理运行时间
**         : 2. 更新当前正在运行的调度实例的虚拟运行时间
**         : 3. 更新当前 cfs 运行队列的 min_vruntime 成员值
**         : 4. 统计当前 cfs 运行队列消耗的物理运行时间，主要在带宽控制时使用
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_clock_task(rq_of(cfs_rq));
	u64 delta_exec;

	if (unlikely(!curr))
		return;

	delta_exec = now - curr->exec_start;
	if (unlikely((s64)delta_exec <= 0))
		return;

	curr->exec_start = now;

	schedstat_set(curr->statistics.exec_max,
		      max(delta_exec, curr->statistics.exec_max));

    /* 更新当前正在运行的调度实例的物理运行时间 */
	curr->sum_exec_runtime += delta_exec;
	schedstat_add(cfs_rq, exec_clock, delta_exec);

    /* 更新当前正在运行的调度实例的虚拟运行时间 */
	curr->vruntime += calc_delta_fair(delta_exec, curr);
	update_min_vruntime(cfs_rq);

	if (entity_is_task(curr)) {
		struct task_struct *curtask = task_of(curr);

		trace_sched_stat_runtime(curtask, delta_exec, curr->vruntime);
		cpuacct_charge(curtask, delta_exec);
		account_group_exec_runtime(curtask, delta_exec);
	}

	account_cfs_rq_runtime(cfs_rq, delta_exec);
}

/*********************************************************************************************************
** 函数名称: update_curr_fair
** 功能描述: 更新指定的 cpu 运行队列的 cfs 运行队列中当前正在运行的调度实例的运行时统计信息
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_curr_fair(struct rq *rq)
{
	update_curr(cfs_rq_of(&rq->curr->se));
}

/*********************************************************************************************************
** 函数名称: update_stats_wait_start
** 功能描述: 更新指定的 cfs 运行队列中指定的调度实例的运行时调度统计信息的 wait_start 字段值
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
update_stats_wait_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->statistics.wait_start, rq_clock(rq_of(cfs_rq)));
}

/*
 * Task is being enqueued - update stats:
 */
/*********************************************************************************************************
** 函数名称: update_stats_enqueue
** 功能描述: 尝试更新指定的 cfs 运行队列中指定的调度实例的运行时调度统计信息的 wait_start 字段值
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_stats_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_start(cfs_rq, se);
}

/*********************************************************************************************************
** 函数名称: update_stats_wait_end
** 功能描述: 更新指定的 cfs 运行队列中指定的调度实例相关的入队等待时间统计信息并结束本次统计
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->statistics.wait_max, max(se->statistics.wait_max,
			rq_clock(rq_of(cfs_rq)) - se->statistics.wait_start));
	schedstat_set(se->statistics.wait_count, se->statistics.wait_count + 1);
	schedstat_set(se->statistics.wait_sum, se->statistics.wait_sum +
			rq_clock(rq_of(cfs_rq)) - se->statistics.wait_start);
#ifdef CONFIG_SCHEDSTATS
	if (entity_is_task(se)) {
		trace_sched_stat_wait(task_of(se),
			rq_clock(rq_of(cfs_rq)) - se->statistics.wait_start);
	}
#endif
	schedstat_set(se->statistics.wait_start, 0);
}

/*********************************************************************************************************
** 函数名称: update_stats_dequeue
** 功能描述: 在从指定的 cfs 运行队列中移除处于等待状态的指定调度实例时更新这个调度实例和入队等待时间
**         : 相关的统计信息并结束本次统计
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
update_stats_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_end(cfs_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
/*********************************************************************************************************
** 函数名称: update_stats_curr_start
** 功能描述: 更新指定的 cfs 运行队列中指定的调度实例的开始运行时的时钟信息
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
update_stats_curr_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_clock_task(rq_of(cfs_rq));
}

/**************************************************
 * Scheduling class queueing methods:
 */

#ifdef CONFIG_NUMA_BALANCING
/*
 * Approximate time to scan a full NUMA task in ms. The task scan period is
 * calculated based on the tasks virtual memory size and
 * numa_balancing_scan_size.
 */
unsigned int sysctl_numa_balancing_scan_period_min = 1000;
unsigned int sysctl_numa_balancing_scan_period_max = 60000;

/* Portion of address space to scan in MB */
unsigned int sysctl_numa_balancing_scan_size = 256;

/* Scan @scan_size MB every @scan_period after an initial @scan_delay in ms */
unsigned int sysctl_numa_balancing_scan_delay = 1000;

/*********************************************************************************************************
** 函数名称: task_nr_scan_windows
** 功能描述: 根据指定任务占用的物理内存页数计算一轮内存扫描需要占用多少个扫描窗口
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned int - 扫描窗口个数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned int task_nr_scan_windows(struct task_struct *p)
{
	unsigned long rss = 0;
	unsigned long nr_scan_pages;

	/*
	 * Calculations based on RSS as non-present and empty pages are skipped
	 * by the PTE scanner and NUMA hinting faults should be trapped based
	 * on resident pages
	 */
	nr_scan_pages = sysctl_numa_balancing_scan_size << (20 - PAGE_SHIFT);
	rss = get_mm_rss(p->mm);
	if (!rss)
		rss = nr_scan_pages;

	rss = round_up(rss, nr_scan_pages);
	return rss / nr_scan_pages;
}

/* For sanitys sake, never scan more PTEs than MAX_SCAN_WINDOW MB/sec. */
#define MAX_SCAN_WINDOW 2560

/*********************************************************************************************************
** 函数名称: task_scan_min
** 功能描述: 计算指定任务在扫描一个内存窗口时可以耗费的最小时间
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned int - 最小时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned int task_scan_min(struct task_struct *p)
{
	unsigned int scan_size = ACCESS_ONCE(sysctl_numa_balancing_scan_size);
	unsigned int scan, floor;
	unsigned int windows = 1;

	if (scan_size < MAX_SCAN_WINDOW)
		windows = MAX_SCAN_WINDOW / scan_size;
	floor = 1000 / windows;

	scan = sysctl_numa_balancing_scan_period_min / task_nr_scan_windows(p);
	return max_t(unsigned int, floor, scan);
}

/*********************************************************************************************************
** 函数名称: task_scan_min
** 功能描述: 计算指定任务在扫描一个内存窗口时可以耗费的最大时间
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned int - 最大时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned int task_scan_max(struct task_struct *p)
{
	unsigned int smin = task_scan_min(p);
	unsigned int smax;

	/* Watch for min being lower than max due to floor calculations */
	smax = sysctl_numa_balancing_scan_period_max / task_nr_scan_windows(p);
	return max(smin, smax);
}

/*********************************************************************************************************
** 函数名称: account_numa_enqueue
** 功能描述: 在任务入队列时根据指定任务的 numa 数据更新指定的 cpu 运行队列中和 numa 相关的统计信息
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void account_numa_enqueue(struct rq *rq, struct task_struct *p)
{
	rq->nr_numa_running += (p->numa_preferred_nid != -1);
	rq->nr_preferred_running += (p->numa_preferred_nid == task_node(p));
}

/*********************************************************************************************************
** 函数名称: account_numa_enqueue
** 功能描述: 在任务出队列时根据指定任务的 numa 数据更新指定的 cpu 运行队列中和 numa 相关的统计信息
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void account_numa_dequeue(struct rq *rq, struct task_struct *p)
{
	rq->nr_numa_running -= (p->numa_preferred_nid != -1);
	rq->nr_preferred_running -= (p->numa_preferred_nid == task_node(p));
}

struct numa_group {
    /* 表示当前 numa 组的引用计数值 */
	atomic_t refcount;

	spinlock_t lock; /* nr_tasks, tasks */

	/* 表示当前 numa 组内包含的任务个数 */
	int nr_tasks;

	/* 表示当前 numa 组 id 值 */
	pid_t gid;

	struct rcu_head rcu;

	/* 表示在当前 numa 组内发生 numa_pte faults 比较多的 node 节点掩码值
	   详情见函数 update_numa_active_node_mask */
	nodemask_t active_nodes;
	
	/* 表示在 numa_group.faults 数组中所有成员的 numa_pte faults 的总和 */
	unsigned long total_faults;
	
	/*
	 * Faults_cpu is used to decide whether memory should move
	 * towards the CPU. As a consequence, these stats are weighted
	 * more by CPU use than by memory faults.
	 */
    /* faults_cpu 是用来判断内存是否需要移动到指定 cpu 处，因此，这些统计数据
       更注重的是 cpu 对物理内存的使用率而不是发生 pte faults 的物理内存页数 */
	unsigned long *faults_cpu;

	/* numa_faults 是按照指定顺序划分成四个区域的数组指针，划分顺序分别是
	 * faults_memory, faults_cpu, faults_memory_buffer, faults_cpu_buffer
	 * 
	 * faults_memory：每个节点上内存访问故障的指数衰减平均值。调度放置决策是
	 *                基于这些计数计算得出的。在 PTE 扫描期间，这些值保持不变。
	 *
	 * faults_cpu：在提示发生 NUMA faults 时记录进程所在的 node id 值
	 * 
	 * faults_memory_buffer 和 faults_cpu_buffer：在当前扫描窗口中记录每个节点
	 * 的错误。当扫描完成时，faults_memory 和 faults_cpu 中的计数会衰减，并复制
	 * 这些值。在每个扫描窗口中会清零重新开始计数。
	 * 
	 * numa_faults 的物理布局如下：
	 * 
	 * --------------------------------------
	 * |             |           |  share   |
	 * |             |   node 0  |  private |
	 * |             |----------------------|
	 * |             |    ...    |  share   |
	 * |  NUMA_MEM   |   node i  |  private |
	 * |             |----------------------|
	 * |             |           |  share   |
	 * |             |   node n  |  private |
	 * |-------------|----------------------|
	 * |             |           |  share   |
	 * |             |   node 0  |  private |
	 * |             |----------------------|
	 * |             |    ...    |  share   |
	 * |  NUMA_CPU   |   node i  |  private |
	 * |             |----------------------|
	 * |             |           |  share   |
	 * |             |   node n  |  private |
	 * |-------------|----------------------|
	 * |             |           |  share   |
	 * |             |   node 0  |  private |
	 * |             |----------------------|
	 * |             |    ...    |  share   |
	 * | NUMA_MEMBUF |   node i  |  private |
	 * |             |----------------------|
	 * |             |           |  share   |
	 * |             |   node n  |  private |
	 * |-------------|----------------------|
	 * |             |           |  share   |
	 * |             |   node 0  |  private |
	 * |             |----------------------|
	 * |             |    ...    |  share   |
	 * | NUMA_CPUBUF |   node i  |  private |
	 * |             |----------------------|
	 * |             |           |  share   |
	 * |             |   node n  |  private |
	 * |-------------|----------------------| 
	 */

	/* 按照指定顺序划分成四个 faults 区域的数组指针，存储了当前 numa 组内所有成员
	   发生过的 numa_pte faults 的物理内存页个数，触发 numa_pte faults 的函数是 task_numa_work */
	unsigned long faults[0];
};

/* Shared or private faults. */
#define NR_NUMA_HINT_FAULT_TYPES 2

/* Memory and CPU locality */
#define NR_NUMA_HINT_FAULT_STATS (NR_NUMA_HINT_FAULT_TYPES * 2)   /* 4 */

/* Averaged statistics, and temporary buffers. */
#define NR_NUMA_HINT_FAULT_BUCKETS (NR_NUMA_HINT_FAULT_STATS * 2) /* 8 */

/*********************************************************************************************************
** 函数名称: task_numa_group_id
** 功能描述: 获取指定任务所属 numa 组的组 id
** 输	 入: p - 指定的任务指针
** 输	 出: pid_t - 组 id
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
pid_t task_numa_group_id(struct task_struct *p)
{
	return p->numa_group ? p->numa_group->gid : 0;
}

/*
 * The averaged statistics, shared & private, memory & cpu,
 * occupy the first half of the array. The second half of the
 * array is for current counters, which are averaged into the
 * first set by task_numa_placement.
 */
/*********************************************************************************************************
** 函数名称: task_faults_idx
** 功能描述: 通过指定的参数计算出在 task_struct.numa_faults/task_struct.numa_group->faults 或者
**         : task_struct.numa_group->faults_cpu 数组中对应的索引值
** 输	 入: s - 指定的 faults 类型
**         : nid - 指定的 node id
**         : priv - 表示是否为私有 faults
** 输	 出: int - 对应的数组索引值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_faults_idx(enum numa_faults_stats s, int nid, int priv)
{
	return NR_NUMA_HINT_FAULT_TYPES * (s * nr_node_ids + nid) + priv;
}

/*********************************************************************************************************
** 函数名称: task_faults
** 功能描述: 计算指定的任务在指定的 node 节点上发生过 NUMA_MEM 类型的 task pte faults 的物理内存页个数
** 输	 入: p - 指定的任务指针
**         : nid - 指定的 node id
** 输	 出: int - 发生过 NUMA_MEM 类型的 pte faults 的物理内存页个数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long task_faults(struct task_struct *p, int nid)
{
	if (!p->numa_faults)
		return 0;

	return p->numa_faults[task_faults_idx(NUMA_MEM, nid, 0)] +
		p->numa_faults[task_faults_idx(NUMA_MEM, nid, 1)];
}

/*********************************************************************************************************
** 函数名称: task_faults
** 功能描述: 计算指定的任务在指定的 node 节点上发生过 NUMA_MEM 类型的 group pte faults 的物理内存页个数
** 输	 入: p - 指定的任务指针
**         : nid - 指定的 node id
** 输	 出: int - 发生过 numa_group_pte faults 的物理内存页个数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long group_faults(struct task_struct *p, int nid)
{
	if (!p->numa_group)
		return 0;

	return p->numa_group->faults[task_faults_idx(NUMA_MEM, nid, 0)] +
		p->numa_group->faults[task_faults_idx(NUMA_MEM, nid, 1)];
}

/*********************************************************************************************************
** 函数名称: task_faults
** 功能描述: 计算指定的任务在指定的 node 节点上发生过 NUMA_MEM 类型的 group cpu pte faults 的物理内存页个数
** 输	 入: group - 指定的 numa 组指针
**         : nid - 指定的 node id
** 输	 出: int - 发生过 numa_pte faults 的物理内存页个数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long group_faults_cpu(struct numa_group *group, int nid)
{
	return group->faults_cpu[task_faults_idx(NUMA_MEM, nid, 0)] +
		group->faults_cpu[task_faults_idx(NUMA_MEM, nid, 1)];
}

/* Handle placement on systems where not all nodes are directly connected. */
/*********************************************************************************************************
** 函数名称: score_nearby_nodes
** 功能描述: 计算指定的任务/任务组在指定的 node 附近所有 N_ONLINE node 节点上发生了 numa_pte 
**         : faults 的物理内存页个数
** 输	 入: p - 指定的任务/任务组指针
**         : nid - 指定的 node id
**         : maxdist - 为 NUMA_BACKPLANE 拓扑类型指定的最大统计范围距离
**         : task - 表示指定的 p 是否是任务（除了任务还有任务组）
** 输	 出: score - 发生过 numa_pte faults 的物理内存页个数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long score_nearby_nodes(struct task_struct *p, int nid,
					int maxdist, bool task)
{
	unsigned long score = 0;
	int node;

	/*
	 * All nodes are directly connected, and the same distance
	 * from each other. No need for fancy placement algorithms.
	 */
	if (sched_numa_topology_type == NUMA_DIRECT)
		return 0;

	/*
	 * This code is called for each node, introducing N^2 complexity,
	 * which should be ok given the number of nodes rarely exceeds 8.
	 */
	/* 遍历当前系统内处于 N_ONLINE 状态下的每一个 node 节点 */
	for_each_online_node(node) {
		unsigned long faults;

		/* 获取从指定的起始 numa node 到指定的目的 numa node 之间的距离 */
		int dist = node_distance(nid, node);

		/*
		 * The furthest away nodes in the system are not interesting
		 * for placement; nid was already counted.
		 */
		if (dist == sched_max_numa_distance || node == nid)
			continue;

		/*
		 * On systems with a backplane NUMA topology, compare groups
		 * of nodes, and move tasks towards the group with the most
		 * memory accesses. When comparing two nodes at distance
		 * "hoplimit", only nodes closer by than "hoplimit" are part
		 * of each group. Skip other nodes.
		 */
		if (sched_numa_topology_type == NUMA_BACKPLANE &&
					dist > maxdist)
			continue;

		/* Add up the faults from nearby nodes. */
		if (task)
			faults = task_faults(p, node);
		else
			faults = group_faults(p, node);

		/*
		 * On systems with a glueless mesh NUMA topology, there are
		 * no fixed "groups of nodes". Instead, nodes that are not
		 * directly connected bounce traffic through intermediate
		 * nodes; a numa_group can occupy any set of nodes.
		 * The further away a node is, the less the faults count.
		 * This seems to result in good task placement.
		 */
		if (sched_numa_topology_type == NUMA_GLUELESS_MESH) {
			faults *= (sched_max_numa_distance - dist);
			faults /= (sched_max_numa_distance - LOCAL_DISTANCE);
		}

		score += faults;
	}

	return score;
}

/*
 * These return the fraction of accesses done by a particular task, or
 * task group, on a particular numa node.  The group weight is given a
 * larger multiplier, in order to group tasks together that are almost
 * evenly spread out between numa nodes.
 */
/*********************************************************************************************************
** 函数名称: task_weight
** 功能描述: 计算指定的任务在指定的 node 节点上的 numa_pte faults 占这个任务在指定距离内的所有 node 
**         : 上的 numa_pte faults 的比例值，比例越高，内存访问性能越好
** 输	 入: p - 指定的任务指针
**         : nid - 指定的 node id
**         : maxdist - 为 NUMA_BACKPLANE 拓扑类型指定的最大统计范围距离
** 输	 出: unsigned long - 计算到的 numa_pte faults 权重信息值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long task_weight(struct task_struct *p, int nid,
					int dist)
{
	unsigned long faults, total_faults;

	if (!p->numa_faults)
		return 0;

	total_faults = p->total_numa_faults;

	if (!total_faults)
		return 0;

    /* 计算指定的任务一共发生了 task pte faults 的内存页数 */
	faults = task_faults(p, nid);
	faults += score_nearby_nodes(p, nid, dist, true);

	return 1000 * faults / total_faults;
}

/*********************************************************************************************************
** 函数名称: group_weight
** 功能描述: 计算指定的任务组在指定的 node 节点上的 numa_group_pte faults 占这个任务在指定距离内的
**         : 所有 node 上的 numa_group_pte faults 的比例值，比例越高，内存访问性能越好
** 输	 入: p - 指定的任务组指针
**         : nid - 指定的 node id
**         : maxdist - 为 NUMA_BACKPLANE 拓扑类型指定的最大统计范围距离
** 输	 出: unsigned long - 计算到的 numa_group_pte faults 权重信息值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long group_weight(struct task_struct *p, int nid,
					 int dist)
{
	unsigned long faults, total_faults;

	if (!p->numa_group)
		return 0;

	total_faults = p->numa_group->total_faults;

	if (!total_faults)
		return 0;

    /* 计算指定的任务一共发生了 group pte faults 的内存页数 */
	faults = group_faults(p, nid);
	faults += score_nearby_nodes(p, nid, dist, false);

	return 1000 * faults / total_faults;
}

/*********************************************************************************************************
** 函数名称: should_numa_migrate_memory
** 功能描述: 判断指定任务中指定的物理内存页是否需要从指定的源 node 内存空间中迁移到指定的目的
**         : cpu 所在 node 的内存空间中
** 注     释: 我们通过将内存访问的物理内存页平均分布到不同的 node 上可以分散内存访问负载，提高内存访问带宽
** 输	 入: p - 指定的任务组指针
**         : page - 指定的物理内存页指针
**         : src_nid - 指定的源 node id 值
**         : dst_cpu - 指定的目的 cpu id 值
** 输	 出: 1 - 需要迁移
**         ：0 - 不需要迁移
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
bool should_numa_migrate_memory(struct task_struct *p, struct page * page,
				int src_nid, int dst_cpu)
{
	struct numa_group *ng = p->numa_group;
	int dst_nid = cpu_to_node(dst_cpu);
	int last_cpupid, this_cpupid;

	this_cpupid = cpu_pid_to_cpupid(dst_cpu, current->pid);

	/*
	 * Multi-stage node selection is used in conjunction with a periodic
	 * migration fault to build a temporal task<->page relation. By using
	 * a two-stage filter we remove short/unlikely relations.
	 *
	 * Using P(p) ~ n_p / n_t as per frequentist probability, we can equate
	 * a task's usage of a particular page (n_p) per total usage of this
	 * page (n_t) (in a given time-span) to a probability.
	 *
	 * Our periodic faults will sample this probability and getting the
	 * same result twice in a row, given these samples are fully
	 * independent, is then given by P(n)^2, provided our sample period
	 * is sufficiently short compared to the usage pattern.
	 *
	 * This quadric squishes small probabilities, making it less likely we
	 * act on an unlikely task<->page relation.
	 */
	/* 设置指定的物理内存页的 _last_cpupid 字段为指定的值并返回原来的旧值 */
	last_cpupid = page_cpupid_xchg_last(page, this_cpupid);

	/* 如果上一次访问这个物理内存页的进程的 node id 不等于指定的目的 node id 则不迁移内存 */
	if (!cpupid_pid_unset(last_cpupid) &&
				cpupid_to_nid(last_cpupid) != dst_nid)
		return false;

	/* Always allow migrate on private faults */
	if (cpupid_match_pid(p, last_cpupid))
		return true;

	/* A shared fault, but p->numa_group has not been set up yet. */
	if (!ng)
		return true;

	/*
	 * Do not migrate if the destination is not a node that
	 * is actively used by this numa group.
	 */
	if (!node_isset(dst_nid, ng->active_nodes))
		return false;

	/*
	 * Source is a node that is not actively used by this
	 * numa group, while the destination is. Migrate.
	 */
	if (!node_isset(src_nid, ng->active_nodes))
		return true;

	/*
	 * Both source and destination are nodes in active
	 * use by this numa group. Maximize memory bandwidth
	 * by migrating from more heavily used groups, to less
	 * heavily used ones, spreading the load around.
	 * Use a 1/4 hysteresis to avoid spurious page movement.
	 */
	/* 我们通过将内存访问的物理内存页平均分布到不同的 node 上可以分散内存访问负载，提高内存访问带宽 */
	return group_faults(p, dst_nid) < (group_faults(p, src_nid) * 3 / 4);
}

static unsigned long weighted_cpuload(const int cpu);
static unsigned long source_load(int cpu, int type);
static unsigned long target_load(int cpu, int type);
static unsigned long capacity_of(int cpu);
static long effective_load(struct task_group *tg, int cpu, long wl, long wg);

/* Cached statistics for all CPUs within a node */
struct numa_stats {
    /* 表示当前 node 上所有 cpu 上的调度实例总和 */
	unsigned long nr_running;
	
    /* 表示当前 node 上所有 cpu 上的 cfs 运行队列的衰减加权负载贡献总和 */
	unsigned long load;

	/* Total compute capacity of CPUs on a node */	
    /* 表示当前 node 上所有 cpu 的算力总和 */
	unsigned long compute_capacity;

	/* Approximate capacity in terms of runnable tasks on a node */
	/* 表示当前 node 上所有 cpu 可以运行的任务数量总和 */
	unsigned long task_capacity;

    /* 表示当前 node 上所有 cpu 是否还有剩余算力 */
	int has_free_capacity;
};

/*
 * XXX borrowed from update_sg_lb_stats
 */
/*********************************************************************************************************
** 函数名称: should_numa_migrate_memory
** 功能描述: 根据指定 node id 的运行状态更新与其对应的 numa_stats 数据信息
** 输	 入: nid - 指定的 node id 值
**         : ns - 需要更新的 numa_stats 结构指针
** 输	 出: ns - 更新后的 numa_stats 结构数据
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_numa_stats(struct numa_stats *ns, int nid)
{
	int smt, cpu, cpus = 0;
	unsigned long capacity;

	memset(ns, 0, sizeof(*ns));
	for_each_cpu(cpu, cpumask_of_node(nid)) {
		struct rq *rq = cpu_rq(cpu);

		ns->nr_running += rq->nr_running;
		ns->load += weighted_cpuload(cpu);
		ns->compute_capacity += capacity_of(cpu);

		cpus++;
	}

	/*
	 * If we raced with hotplug and there are no CPUs left in our mask
	 * the @ns structure is NULL'ed and task_numa_compare() will
	 * not find this node attractive.
	 *
	 * We'll either bail at !has_free_capacity, or we'll detect a huge
	 * imbalance and bail there.
	 */
	if (!cpus)
		return;

	/* smt := ceil(cpus / capacity), assumes: 1 < smt_power < 2 */
	smt = DIV_ROUND_UP(SCHED_CAPACITY_SCALE * cpus, ns->compute_capacity);
	capacity = cpus / smt; /* cores */

	ns->task_capacity = min_t(unsigned, capacity,
		DIV_ROUND_CLOSEST(ns->compute_capacity, SCHED_CAPACITY_SCALE));
	ns->has_free_capacity = (ns->nr_running < ns->task_capacity);
}

struct task_numa_env {
    /* 表示需要进行迁移的任务指针 */
	struct task_struct *p;

    /* 表示任务迁移起点的 cpu id 以及 node id 信息 */
	int src_cpu, src_nid;

	/* 表示任务迁移终点的 cpu id 以及 node id 信息 */
	int dst_cpu, dst_nid;

    /* 分别任务迁移起点 node 的 numa_stats 数据和任务迁移终点 node 的 numa_stats 数据 */
	struct numa_stats src_stats, dst_stats;

    /* 表示允许的算力负载失衡阈值百分比 */
	int imbalance_pct;

	/* 为 NUMA_BACKPLANE 拓扑类型指定的最大统计范围距离 */
	int dist;

    /* 在内存负载迁移过程中，记录用来交换迁移的“目的任务”指针，如果为 NULL 表示只做“源任务”
       到目的 cpu 上的单向迁移，不需要执行双向交换迁移 */
	struct task_struct *best_task;

    /* 表示当前内存负载迁移过程中，最大的内存负载增量值（这个值越大，表示内存访问性能越高）*/
	long best_imp;

	/* 在内存负载迁移过程中，记录内存负载增量值最大的目的 cpu id 值 */
	int best_cpu;
};

/*********************************************************************************************************
** 函数名称: task_numa_assign
** 功能描述: 为指定的 task_numa_env 分配最优内存负载迁移任务信息
** 输	 入: env - 指定的 task_numa_env 结构指针
**         : p - 指定的最优任务指针
**         : imp - 指定的最优 imp 数据
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_numa_assign(struct task_numa_env *env,
			     struct task_struct *p, long imp)
{
	if (env->best_task)
		put_task_struct(env->best_task);
	if (p)
		get_task_struct(p);

	env->best_task = p;
	env->best_imp = imp;
	env->best_cpu = env->dst_cpu;
}

/*********************************************************************************************************
** 函数名称: load_too_imbalanced
** 功能描述: 根据指定的负载数据计算在指定的 task_numa_env 环境下是否负载失衡且超过设定的阈值
**         : 如果之前负载已经失衡，则返回指定的负载是否会使负载失衡状态更严重
** 输	 入: src_load - 指定的源负载数据
**         : dst_load - 指定的目的负载数据
**         : env - task_numa_env 结构指针
** 输	 出: 1 - 负载失衡且变的更糟糕
**         : 0 - 负载没失衡或者失衡情况转好
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool load_too_imbalanced(long src_load, long dst_load,
				struct task_numa_env *env)
{
	long imb, old_imb;
	long orig_src_load, orig_dst_load;
	long src_capacity, dst_capacity;

	/*
	 * The load is corrected for the CPU capacity available on each node.
	 *
	 * src_load        dst_load
	 * ------------ vs ---------
	 * src_capacity    dst_capacity
	 */
	/* src_load         dst_load
      ------------  -  ------------  >  0 得出
      src_capacity     dst_capacity

      src_load * dst_capacity - dst_load * src_capacity > 0 得出
      dst_load * src_capacity - src_load * dst_capacity < 0 */
      
	src_capacity = env->src_stats.compute_capacity;
	dst_capacity = env->dst_stats.compute_capacity;

	/* We care about the slope of the imbalance, not the direction. */
	if (dst_load < src_load)
		swap(dst_load, src_load);

	/* Is the difference below the threshold? */
	/* 指定的负载数据计算是否超过我们设置的阈值 */
	imb = dst_load * src_capacity * 100 -
	      src_load * dst_capacity * env->imbalance_pct;
	if (imb <= 0)
		return false;

	/*
	 * The imbalance is above the allowed threshold.
	 * Compare it with the old imbalance.
	 */
	orig_src_load = env->src_stats.load;
	orig_dst_load = env->dst_stats.load;

	if (orig_dst_load < orig_src_load)
		swap(orig_dst_load, orig_src_load);

    /* 计算之前的负载数据的负载失衡系数 */
	old_imb = orig_dst_load * src_capacity * 100 -
		  orig_src_load * dst_capacity * env->imbalance_pct;

	/* Would this change make things worse? */
    /* 比较指定负载数据的失衡系数是否要比之前的负载失衡系数大，如果变大表示
	   负载状态会变得更糟糕，则返回 1 */
	return (imb > old_imb);
}

/*
 * This checks if the overall compute and NUMA accesses of the system would
 * be improved if the source tasks was migrated to the target dst_cpu taking
 * into account that it might be best if task running on the dst_cpu should
 * be exchanged with the source task
 */
/*********************************************************************************************************
** 函数名称: task_numa_compare
** 功能描述: 根据指定的 task_numa_env 计算指定的任务迁移前后是否会提高内存负载（内存负载越高，访问
**         : 性能越好），如果可以提高，则更新最优内存负载迁移任务信息
** 输	 入: env - 指定的 task_numa_env 结构指针
**         : taskimp - 指定的源任务在源 node 和目的 node 上的 numa_task_pte faults 差值
**         : groupimp - 指定的源任务组在源 node 和目的 node 上的 numa_group_pte faults 差值
** 输	 出: env - 如果迁移后会有性能提高，则把目标地址信息保存在这里，详细看函数结尾的 task_numa_assign
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_numa_compare(struct task_numa_env *env,
			      long taskimp, long groupimp)
{
	struct rq *src_rq = cpu_rq(env->src_cpu);
	struct rq *dst_rq = cpu_rq(env->dst_cpu);
	struct task_struct *cur;
	long src_load, dst_load;
	long load;
	long imp = env->p->numa_group ? groupimp : taskimp;
	long moveimp = imp;
	int dist = env->dist;

	rcu_read_lock();

	raw_spin_lock_irq(&dst_rq->lock);
	cur = dst_rq->curr;
	/*
	 * No need to move the exiting task, and this ensures that ->curr
	 * wasn't reaped and thus get_task_struct() in task_numa_assign()
	 * is safe under RCU read lock.
	 * Note that rcu_read_lock() itself can't protect from the final
	 * put_task_struct() after the last schedule().
	 */
	if ((cur->flags & PF_EXITING) || is_idle_task(cur))
		cur = NULL;
	raw_spin_unlock_irq(&dst_rq->lock);

	/*
	 * Because we have preemption enabled we can get migrated around and
	 * end try selecting ourselves (current == env->p) as a swap candidate.
	 */
	/* 如果我们想要迁移的“源任务”正在目的 cpu 上执行则直接退出 */
	if (cur == env->p)
		goto unlock;

	/*
	 * "imp" is the fault differential for the source task between the
	 * source and destination node. Calculate the total differential for
	 * the source task and potential destination task. The more negative
	 * the value is, the more rmeote accesses that would be expected to
	 * be incurred if the tasks were swapped.
	 */
	if (cur) {
		/* Skip this swap candidate if cannot move to the source cpu */
		if (!cpumask_test_cpu(env->src_cpu, tsk_cpus_allowed(cur)))
			goto unlock;

		/*
		 * If dst and source tasks are in the same NUMA group, or not
		 * in any group then look only at task weights.
		 */
		/* 如果两个任务在相同的 numa 组内或者都不属于任何 numa 组，则计算迁移前后“总的”任务内存负载
		   增量值，增量值越大，内存访问性能越高 */
		if (cur->numa_group == env->p->numa_group) {
			imp = taskimp + task_weight(cur, env->src_nid, dist) -
			      task_weight(cur, env->dst_nid, dist);
			/*
			 * Add some hysteresis to prevent swapping the
			 * tasks within a group over tiny differences.
			 */
			if (cur->numa_group)
				imp -= imp/16;
		} else {
			/*
			 * Compare the group weights. If a task is all by
			 * itself (not part of a group), use the task weight
			 * instead.
			 */
			/* 如果两个任务不在相同的 numa 组内则计算迁移前后“总的”任务内存负载变化或者“总的” numa 组
			   内存负载增量值，增量值越大，内存访问性能越高 */
			if (cur->numa_group)
				imp += group_weight(cur, env->src_nid, dist) -
				       group_weight(cur, env->dst_nid, dist);
			else
				imp += task_weight(cur, env->src_nid, dist) -
				       task_weight(cur, env->dst_nid, dist);
		}
	}

    /* 如果迁移前后“总的”内存负载增量以及迁移前后“源”任务内存负载增量都在都比较小（不会有太大内存性能提升），则直接返回 */
	if (imp <= env->best_imp && moveimp <= env->best_imp)
		goto unlock;

    /* 如果目的 cpu 当前为空闲状态且目的 cpu 有空闲算力，则执行负载均衡操作，否则直接返回 */
	if (!cur) {
		/* Is there capacity at our destination? */
		if (env->src_stats.nr_running <= env->src_stats.task_capacity &&
		    !env->dst_stats.has_free_capacity)
			goto unlock;

		goto balance;
	}

	/* Balance doesn't matter much if we're running a task per cpu */
	/* 如果源 cpu 和目的 cpu 上都只有一个任务在运行且“总的”内存负载增量值 imp > env->best_imp 则更新
	   当前 task_numa_env 的最优内存负载迁移任务信息并返回 */
	if (imp > env->best_imp && src_rq->nr_running == 1 &&
			dst_rq->nr_running == 1)
		goto assign;

	/*
	 * In the overloaded case, try and keep the load balanced.
	 */
balance:
	load = task_h_load(env->p);
	dst_load = env->dst_stats.load + load;
	src_load = env->src_stats.load - load;

    /* moveimp - 表示只有“源任务”执行迁移前后“总的”内存负载增量值
       imp - 表示“源任务”和“目标任务”同时迁移前后“总的”内存负载增量值 */
	if (moveimp > imp && moveimp > env->best_imp) {
		/*
		 * If the improvement from just moving env->p direction is
		 * better than swapping tasks around, check if a move is
		 * possible. Store a slightly smaller score than moveimp,
		 * so an actually idle CPU will win.
		 */
		/* 如果只迁移“源任务”会有更高的内存负载增量值，则判断这种情况下算力负载是否均衡
		   如果算力负载仍然均衡，则只迁移“源任务” */
		if (!load_too_imbalanced(src_load, dst_load, env)) {
			imp = moveimp - 1;
			cur = NULL;
			goto assign;
		}
	}

	if (imp <= env->best_imp)
		goto unlock;

	if (cur) {
		load = task_h_load(cur);
		dst_load -= load;
		src_load += load;
	}

    /* 计算在“源任务”和“目标任务”同时迁移前后负载失衡且变的更糟糕则直接返回 */
	if (load_too_imbalanced(src_load, dst_load, env))
		goto unlock;

	/*
	 * One idle CPU per node is evaluated for a task numa move.
	 * Call select_idle_sibling to maybe find a better one.
	 */
	if (!cur)
		env->dst_cpu = select_idle_sibling(env->p, env->dst_cpu);

assign:
    /* 为指定的 task_numa_env 分配最优内存负载迁移任务信息 */
	task_numa_assign(env, cur, imp);
unlock:
	rcu_read_unlock();
}

/*********************************************************************************************************
** 函数名称: task_numa_compare
** 功能描述: 根据指定的 task_numa_env 计算指定的任务如果执行任务迁移，在指定的目标 numa 节点上的最优 cpu 位置
** 输	 入: env - 指定的 task_numa_env 结构指针
**         : taskimp - 指定的源任务在源 node 和目的 node 上的 numa pte faults 差值
**         : groupimp - 指定的源任务组在源 node 和目的 node 上的 numa pte faults 差值
** 输	 出: env - 把目标节点上的最优 cpu 地址信息存储在这里，详细看 task_numa_compare 函数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_numa_find_cpu(struct task_numa_env *env,
				long taskimp, long groupimp)
{
	int cpu;

	for_each_cpu(cpu, cpumask_of_node(env->dst_nid)) {
		/* Skip this CPU if the source task cannot migrate */
		if (!cpumask_test_cpu(cpu, tsk_cpus_allowed(env->p)))
			continue;

		env->dst_cpu = cpu;
		task_numa_compare(env, taskimp, groupimp);
	}
}

/*********************************************************************************************************
** 函数名称: task_numa_migrate
** 功能描述: 在当前系统内为指定的需要迁移的任务尝试找到一个最优迁移目的 node & cpu 并执行迁移操作
** 输	 入: p - 指定的任务指针
** 输	 出: 0 - 迁移成功
**         : other - 迁移失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int task_numa_migrate(struct task_struct *p)
{
	struct task_numa_env env = {
		.p = p,

		.src_cpu = task_cpu(p),
		.src_nid = task_node(p),

		.imbalance_pct = 112,

		.best_task = NULL,
		.best_imp = 0,
		.best_cpu = -1
	};
	struct sched_domain *sd;
	unsigned long taskweight, groupweight;
	int nid, ret, dist;
	long taskimp, groupimp;

	/*
	 * Pick the lowest SD_NUMA domain, as that would have the smallest
	 * imbalance and would be the first to start moving tasks about.
	 *
	 * And we want to avoid any moving of tasks about, as that would create
	 * random movement of tasks -- counter the numa conditions we're trying
	 * to satisfy here.
	 */
	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_numa, env.src_cpu));
	if (sd)
		env.imbalance_pct = 100 + (sd->imbalance_pct - 100) / 2;
	rcu_read_unlock();

	/*
	 * Cpusets can break the scheduler domain tree into smaller
	 * balance domains, some of which do not cross NUMA boundaries.
	 * Tasks that are "trapped" in such domains cannot be migrated
	 * elsewhere, so there is no point in (re)trying.
	 */
	if (unlikely(!sd)) {
		p->numa_preferred_nid = task_node(p);
		return -EINVAL;
	}

	/* 在需要执行任务迁移的任务的 preferred node 上查找最优的迁移目标 cpu */
	env.dst_nid = p->numa_preferred_nid;

	dist = env.dist = node_distance(env.src_nid, env.dst_nid);
	taskweight = task_weight(p, env.src_nid, dist);
	groupweight = group_weight(p, env.src_nid, dist);
	update_numa_stats(&env.src_stats, env.src_nid);
	taskimp = task_weight(p, env.dst_nid, dist) - taskweight;
	groupimp = group_weight(p, env.dst_nid, dist) - groupweight;
	update_numa_stats(&env.dst_stats, env.dst_nid);

	/* Try to find a spot on the preferred nid. */
	task_numa_find_cpu(&env, taskimp, groupimp);

	/*
	 * Look at other nodes in these cases:
	 * - there is no space available on the preferred_nid
	 * - the task is part of a numa_group that is interleaved across
	 *   multiple NUMA nodes; in order to better consolidate the group,
	 *   we need to check other locations.
	 */
	
	/* 如果在指定的任务的 preferred node 上没找到最优的迁移目标 cpu 则在 preferred node 
	   之外的 node 上查找最优的迁移目标 cpu */
	if (env.best_cpu == -1 || (p->numa_group &&
			nodes_weight(p->numa_group->active_nodes) > 1)) {
		for_each_online_node(nid) {
			if (nid == env.src_nid || nid == p->numa_preferred_nid)
				continue;

			dist = node_distance(env.src_nid, env.dst_nid);
			if (sched_numa_topology_type == NUMA_BACKPLANE &&
						dist != env.dist) {
				taskweight = task_weight(p, env.src_nid, dist);
				groupweight = group_weight(p, env.src_nid, dist);
			}

			/* Only consider nodes where both task and groups benefit */
			taskimp = task_weight(p, nid, dist) - taskweight;
			groupimp = group_weight(p, nid, dist) - groupweight;
			if (taskimp < 0 && groupimp < 0)
				continue;

			env.dist = dist;
			env.dst_nid = nid;
			update_numa_stats(&env.dst_stats, env.dst_nid);
			task_numa_find_cpu(&env, taskimp, groupimp);
		}
	}

	/*
	 * If the task is part of a workload that spans multiple NUMA nodes,
	 * and is migrating into one of the workload's active nodes, remember
	 * this node as the task's preferred numa node, so the workload can
	 * settle down.
	 * A task that migrated to a second choice node will be better off
	 * trying for a better one later. Do not set the preferred node here.
	 */
	if (p->numa_group) {
		if (env.best_cpu == -1)
			nid = env.src_nid;
		else
			nid = env.dst_nid;

		if (node_isset(nid, p->numa_group->active_nodes))
			sched_setnuma(p, env.dst_nid);
	}

	/* No better CPU than the current one was found. */
	if (env.best_cpu == -1)
		return -EAGAIN;

	/*
	 * Reset the scan period if the task is being rescheduled on an
	 * alternative node to recheck if the tasks is now properly placed.
	 */
	p->numa_scan_period = task_scan_min(p);

    /* env.best_task 在内存负载迁移过程中，记录用来交换迁移的“目的任务”指针，如果为 NULL 表示
       只做“源任务”到目的 cpu 上的单向迁移，不需要执行双向交换迁移 */
	if (env.best_task == NULL) {
		ret = migrate_task_to(p, env.best_cpu);
		if (ret != 0)
			trace_sched_stick_numa(p, env.src_cpu, env.best_cpu);
		return ret;
	}

	ret = migrate_swap(p, env.best_task);
	if (ret != 0)
		trace_sched_stick_numa(p, env.src_cpu, task_cpu(env.best_task));
	put_task_struct(env.best_task);
	return ret;
}

/* Attempt to migrate a task to a CPU on the preferred node. */
/*********************************************************************************************************
** 函数名称: task_numa_migrate
** 功能描述: 尝试把指定的任务迁移到这个任务的 preferred node 上的最优 cpu 上运行
** 输	 入: p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void numa_migrate_preferred(struct task_struct *p)
{
	unsigned long interval = HZ;

	/* This task has no NUMA fault statistics yet */
	if (unlikely(p->numa_preferred_nid == -1 || !p->numa_faults))
		return;

	/* Periodically retry migrating the task to the preferred node */
	interval = min(interval, msecs_to_jiffies(p->numa_scan_period) / 16);
	p->numa_migrate_retry = jiffies + interval;

	/* Success if task is already running on preferred CPU */
	if (task_node(p) == p->numa_preferred_nid)
		return;

	/* Otherwise, try migrate to a CPU on the preferred node */
	task_numa_migrate(p);
}

/*
 * Find the nodes on which the workload is actively running. We do this by
 * tracking the nodes from which NUMA hinting faults are triggered. This can
 * be different from the set of nodes where the workload's memory is currently
 * located.
 *
 * The bitmask is used to make smarter decisions on when to do NUMA page
 * migrations, To prevent flip-flopping, and excessive page migrations, nodes
 * are added when they cause over 6/16 of the maximum number of faults, but
 * only removed when they drop below 3/16.
 */
/*********************************************************************************************************
** 函数名称: update_numa_active_node_mask
** 功能描述: 遍历当前系统内所有 online node 节点并根据这些节点在指定的 numa 组内发生的 numa_pte faults
**         : 次数比例来更新指定的 numa 组的 numa_group->active_nodes 字段掩码值
** 输	 入: numa_group - 指定的 numa 组指针
** 输	 出: numa_group - 存储更新后的 numa_group->active_nodes 掩码值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_numa_active_node_mask(struct numa_group *numa_group)
{
	unsigned long faults, max_faults = 0;
	int nid;

    /* 获取当前系统内所有 node 节点在指定的 numa 组内发生 group cpu pte faults 最多的次数 */
	for_each_online_node(nid) {
		faults = group_faults_cpu(numa_group, nid);
		if (faults > max_faults)
			max_faults = faults;
	}

	for_each_online_node(nid) {
		faults = group_faults_cpu(numa_group, nid);
		if (!node_isset(nid, numa_group->active_nodes)) {
			if (faults > max_faults * 6 / 16)
				node_set(nid, numa_group->active_nodes);
		} else if (faults < max_faults * 3 / 16)
			node_clear(nid, numa_group->active_nodes);
	}
}

/*
 * When adapting the scan rate, the period is divided into NUMA_PERIOD_SLOTS
 * increments. The more local the fault statistics are, the higher the scan
 * period will be for the next scan window. If local/(local+remote) ratio is
 * below NUMA_PERIOD_THRESHOLD (where range of ratio is 1..NUMA_PERIOD_SLOTS)
 * the scan period will decrease. Aim for 70% local accesses.
 */
#define NUMA_PERIOD_SLOTS 10
#define NUMA_PERIOD_THRESHOLD 7

/*
 * Increase the scan period (slow down scanning) if the majority of
 * our memory is already on our local node, or if the majority of
 * the page accesses are shared with other processes.
 * Otherwise, decrease the scan period.
 */
/*********************************************************************************************************
** 函数名称: update_task_scan_period
** 功能描述: 根据指定的 shared 和 private numa_pte faults 次数更新指定任务的下一次 numa 扫描周期
** 注     释: 如果指定任务的大部分内存都是本地内存或者是和其他任务共享的内存，则加长 numa 扫描周期
**         : 否则减少指定任务的 numa 扫描周期
** 输	 入: p - 指定的任务指针
**         : shared - 本次扫描周期内发生的 shared numa_pte faults 次数
**         : private - 本次扫描周期内发生的 private numa_pte faults 次数
** 输	 出: p->numa_scan_period - 更新后的下一次 numa 扫描周期
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_task_scan_period(struct task_struct *p,
			unsigned long shared, unsigned long private)
{
	unsigned int period_slot;
	int ratio;
	int diff;

	unsigned long remote = p->numa_faults_locality[0];
	unsigned long local = p->numa_faults_locality[1];

	/*
	 * If there were no record hinting faults then either the task is
	 * completely idle or all activity is areas that are not of interest
	 * to automatic numa balancing. Related to that, if there were failed
	 * migration then it implies we are migrating too quickly or the local
	 * node is overloaded. In either case, scan slower
	 */
	if (local + shared == 0 || p->numa_faults_locality[2]) {
		p->numa_scan_period = min(p->numa_scan_period_max,
			p->numa_scan_period << 1);

		p->mm->numa_next_scan = jiffies +
			msecs_to_jiffies(p->numa_scan_period);

		return;
	}

	/*
	 * Prepare to scale scan period relative to the current period.
	 *	 == NUMA_PERIOD_THRESHOLD scan period stays the same
	 *       <  NUMA_PERIOD_THRESHOLD scan period decreases (scan faster)
	 *	 >= NUMA_PERIOD_THRESHOLD scan period increases (scan slower)
	 */
	 
    /* 计算当前 numa_scan_period 平均到每一个 NUMA_PERIOD_SLOTS 中，每一个 SLOTS 占用的时间长度 */
	period_slot = DIV_ROUND_UP(p->numa_scan_period, NUMA_PERIOD_SLOTS);
	
	ratio = (local * NUMA_PERIOD_SLOTS) / (local + remote);

	/* 如果 local 比例大于百分之 70 则扩大 muna 扫描周期，否则减小 numa 扫描周期 */
	if (ratio >= NUMA_PERIOD_THRESHOLD) {
		int slot = ratio - NUMA_PERIOD_THRESHOLD;
		if (!slot)
			slot = 1;
		diff = slot * period_slot;
	} else {
		diff = -(NUMA_PERIOD_THRESHOLD - ratio) * period_slot;

		/*
		 * Scale scan rate increases based on sharing. There is an
		 * inverse relationship between the degree of sharing and
		 * the adjustment made to the scanning period. Broadly
		 * speaking the intent is that there is little point
		 * scanning faster if shared accesses dominate as it may
		 * simply bounce migrations uselessly
		 */
		ratio = DIV_ROUND_UP(private * NUMA_PERIOD_SLOTS, (private + shared + 1));
		diff = (diff * ratio) / NUMA_PERIOD_SLOTS;
	}

	p->numa_scan_period = clamp(p->numa_scan_period + diff,
			task_scan_min(p), task_scan_max(p));
	memset(p->numa_faults_locality, 0, sizeof(p->numa_faults_locality));
}

/*
 * Get the fraction of time the task has been running since the last
 * NUMA placement cycle. The scheduler keeps similar statistics, but
 * decays those on a 32ms period, which is orders of magnitude off
 * from the dozens-of-seconds NUMA balancing period. Use the scheduler
 * stats only if the task is so new there are no NUMA statistics yet.
 */
/*********************************************************************************************************
** 函数名称: numa_get_avg_runtime
** 功能描述: 获取指定的任务在指定的周期内实际获取到的平均物理运行时间
** 输	 入: p - 指定的任务指针
** 输	 出: period - 表示统计周期
**         : delta - 表示在指定的统计周期内平均物理运行时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 numa_get_avg_runtime(struct task_struct *p, u64 *period)
{
	u64 runtime, delta, now;
	/* Use the start of this time slice to avoid calculations. */
	now = p->se.exec_start;
	runtime = p->se.sum_exec_runtime;

	if (p->last_task_numa_placement) {
		delta = runtime - p->last_sum_exec_runtime;
		*period = now - p->last_task_numa_placement;
	} else {
		delta = p->se.avg.runnable_avg_sum;
		*period = p->se.avg.runnable_avg_period;
	}

	p->last_sum_exec_runtime = runtime;
	p->last_task_numa_placement = now;

	return delta;
}

/*
 * Determine the preferred nid for a task in a numa_group. This needs to
 * be done in a way that produces consistent results with group_weight,
 * otherwise workloads might not converge.
 */
/*********************************************************************************************************
** 函数名称: preferred_group_nid
** 功能描述: 在系统内处于 online 状态的 node 节点中根据 numa_group_pte faults 为指定任务其找出 
**         : preferred node id，找到的 node 是指定任务发生 numa_group_pte faults 次数最多的 node
** 输	 入: p - 指定的任务指针
**         : nid - 指定任务所属任务组发生的 numa_group_pte faults 次数最多的 numa 组 id
** 输	 出: nid - preferred node id
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int preferred_group_nid(struct task_struct *p, int nid)
{
	nodemask_t nodes;
	int dist;

	/* Direct connections between all NUMA nodes. */
	if (sched_numa_topology_type == NUMA_DIRECT)
		return nid;

	/*
	 * On a system with glueless mesh NUMA topology, group_weight
	 * scores nodes according to the number of NUMA hinting faults on
	 * both the node itself, and on nearby nodes.
	 */
	if (sched_numa_topology_type == NUMA_GLUELESS_MESH) {
		unsigned long score, max_score = 0;
		int node, max_node = nid;

		dist = sched_max_numa_distance;

		for_each_online_node(node) {
			score = group_weight(p, node, dist);
			if (score > max_score) {
				max_score = score;
				max_node = node;
			}
		}
		return max_node;
	}

	/*
	 * Finding the preferred nid in a system with NUMA backplane
	 * interconnect topology is more involved. The goal is to locate
	 * tasks from numa_groups near each other in the system, and
	 * untangle workloads from different sides of the system. This requires
	 * searching down the hierarchy of node groups, recursively searching
	 * inside the highest scoring group of nodes. The nodemask tricks
	 * keep the complexity of the search down.
	 */
	nodes = node_online_map;
	for (dist = sched_max_numa_distance; dist > LOCAL_DISTANCE; dist--) {
		unsigned long max_faults = 0;
		nodemask_t max_group = NODE_MASK_NONE;
		int a, b;

		/* Are there nodes at this distance from each other? */
		if (!find_numa_distance(dist))
			continue;
		
		for_each_node_mask(a, nodes) {
			unsigned long faults = 0;
			nodemask_t this_group;
			nodes_clear(this_group);

			/* Sum group's NUMA faults; includes a==b case. */
			for_each_node_mask(b, nodes) {
				if (node_distance(a, b) < dist) {
					faults += group_faults(p, b);
					node_set(b, this_group);
					node_clear(b, nodes);
				}
			}

			/* Remember the top group. */
			if (faults > max_faults) {
				max_faults = faults;
				max_group = this_group;
				/*
				 * subtle: at the smallest distance there is
				 * just one node left in each "group", the
				 * winner is the preferred nid.
				 */
				nid = a;
			}
		}
		
		/* Next round, evaluate the nodes within max_group. */		
        /* nodes - 用来记录当前需要遍历的 numa group 内包含的 node 节点位图（numa 组可能是个树形结构）*/
		nodes = max_group;
	}
	return nid;
}

/*********************************************************************************************************
** 函数名称: task_numa_placement
** 功能描述: 根据指定的任务在系统所有 online node 上发生的 numa_pte faults 数据为其找到 preferred node 
**         : 并根据 preferred node 上每个 cpu 负载情况把这个任务迁移到最优的 cpu 上运行
** 输	 入: p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_numa_placement(struct task_struct *p)
{
	int seq, nid, max_nid = -1, max_group_nid = -1;
	unsigned long max_faults = 0, max_group_faults = 0;
	unsigned long fault_types[2] = { 0, 0 };
	unsigned long total_faults;
	u64 runtime, period;
	spinlock_t *group_lock = NULL;

	seq = ACCESS_ONCE(p->mm->numa_scan_seq);
	if (p->numa_scan_seq == seq)
		return;
	
	p->numa_scan_seq = seq;
	p->numa_scan_period_max = task_scan_max(p);

    /* 表示本次 numa 扫描周期内一共发生的 numa_pte faults 次数 */
	total_faults = p->numa_faults_locality[0] +
		       p->numa_faults_locality[1];
	
	runtime = numa_get_avg_runtime(p, &period);

	/* If the task is part of a group prevent parallel updates to group stats */
	if (p->numa_group) {
		group_lock = &p->numa_group->lock;
		spin_lock_irq(group_lock);
	}

	/* Find the node with the highest number of faults */
	for_each_online_node(nid) {
		/* Keep track of the offsets in numa_faults array */
		int mem_idx, membuf_idx, cpu_idx, cpubuf_idx;
		unsigned long faults = 0, group_faults = 0;
		int priv;

		for (priv = 0; priv < NR_NUMA_HINT_FAULT_TYPES; priv++) {
			long diff, f_diff, f_weight;

			mem_idx = task_faults_idx(NUMA_MEM, nid, priv);
			membuf_idx = task_faults_idx(NUMA_MEMBUF, nid, priv);
			cpu_idx = task_faults_idx(NUMA_CPU, nid, priv);
			cpubuf_idx = task_faults_idx(NUMA_CPUBUF, nid, priv);

			/* Decay existing window, copy faults since last scan */
			diff = p->numa_faults[membuf_idx] - p->numa_faults[mem_idx] / 2;

			/* 记录并清空本次 numa 扫描周期内发生的 mem numa_pte faults 次数记录数据 */
			fault_types[priv] += p->numa_faults[membuf_idx];
			p->numa_faults[membuf_idx] = 0;

			/*
			 * Normalize the faults_from, so all tasks in a group
			 * count according to CPU use, instead of by the raw
			 * number of faults. Tasks with little runtime have
			 * little over-all impact on throughput, and thus their
			 * faults are less important.
			 */
			/* 将 faults_from 规范化，这样组中的所有任务都将根据 CPU 使用情况
			   进行计数，而不是根据错误的原始数量。因为很少运行时的任务对吞吐
			   量的总体影响很小，因此它们的错误就不那么重要了 */
			f_weight = div64_u64(runtime << 16, period + 1);
			f_weight = (f_weight * p->numa_faults[cpubuf_idx]) /
				   (total_faults + 1);
	   
			f_diff = f_weight - p->numa_faults[cpu_idx] / 2;
			
			/* 清空本次 numa 扫描周期内发生的 cpu numa_pte faults 次数记录数据 */
			p->numa_faults[cpubuf_idx] = 0;

            /* p->numa_faults[mem_idx] = p->numa_faults[mem_idx] -
                                        (p->numa_faults[mem_idx] / 2) +
			                             p->numa_faults[membuf_idx]
			   对原有的、旧的 p->numa_faults[mem_idx] 进行减半衰减并加上
			   在新的扫描周期内、新发生的 p->numa_faults[membuf_idx] 统计数据 */
			p->numa_faults[mem_idx] += diff;
			
			p->numa_faults[cpu_idx] += f_diff;
			faults += p->numa_faults[mem_idx];
			p->total_numa_faults += diff;
			
			if (p->numa_group) {
				/*
				 * safe because we can only change our own group
				 *
				 * mem_idx represents the offset for a given
				 * nid and priv in a specific region because it
				 * is at the beginning of the numa_faults array.
				 */
				p->numa_group->faults[mem_idx] += diff;
				p->numa_group->faults_cpu[mem_idx] += f_diff;
				p->numa_group->total_faults += diff;
				group_faults += p->numa_group->faults[mem_idx];
			}
		}

		if (faults > max_faults) {
			max_faults = faults;
			max_nid = nid;
		}

		if (group_faults > max_group_faults) {
			max_group_faults = group_faults;
			max_group_nid = nid;
		}
	}

	update_task_scan_period(p, fault_types[0], fault_types[1]);

	if (p->numa_group) {
		update_numa_active_node_mask(p->numa_group);
		spin_unlock_irq(group_lock);
		max_nid = preferred_group_nid(p, max_group_nid);
	}

	if (max_faults) {
		/* Set the new preferred node */
		if (max_nid != p->numa_preferred_nid)
			sched_setnuma(p, max_nid);

        /* Migrate to the new preferred node */
		if (task_node(p) != p->numa_preferred_nid)
			numa_migrate_preferred(p);
	}
}

/*********************************************************************************************************
** 函数名称: get_numa_group
** 功能描述: 递增指定的 numa 组的引用计数值
** 输	 入: grp - 指定的 numa 组指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int get_numa_group(struct numa_group *grp)
{
	return atomic_inc_not_zero(&grp->refcount);
}

/*********************************************************************************************************
** 函数名称: put_numa_group
** 功能描述: 递减指定的 numa 组的引用计数值并尝试释放其占用的 rcu 资源
** 输	 入: grp - 指定的 numa 组指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void put_numa_group(struct numa_group *grp)
{
	if (atomic_dec_and_test(&grp->refcount))
		kfree_rcu(grp, rcu);
}

/*********************************************************************************************************
** 函数名称: task_numa_group
** 功能描述: 尝试把指定的任务迁移到指定 cpu 上正在运行的任务所属的 numa 任务组内并更新相关数据
** 输	 入: p - 指定的任务指针
**         : cpupid - 指定的任务的 cpupid 标志数据
**         : flags - 指定的 task numa flags
** 输	 出: priv - 它们是否共享内存
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_numa_group(struct task_struct *p, int cpupid, int flags,
			int *priv)
{
	struct numa_group *grp, *my_grp;
	struct task_struct *tsk;
	bool join = false;
	int cpu = cpupid_to_cpu(cpupid);
	int i;

    /* 如果当前任务没有 numa 组，则根据指定的任务状态为其创建并初始化一个 numa 组 */
	if (unlikely(!p->numa_group)) {
		unsigned int size = sizeof(struct numa_group) +
				    4*nr_node_ids*sizeof(unsigned long);

		grp = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
		if (!grp)
			return;

		atomic_set(&grp->refcount, 1);
		spin_lock_init(&grp->lock);
		grp->gid = p->pid;
		/* Second half of the array tracks nids where faults happen */
		grp->faults_cpu = grp->faults + NR_NUMA_HINT_FAULT_TYPES *
						nr_node_ids;

		node_set(task_node(current), grp->active_nodes);

		for (i = 0; i < NR_NUMA_HINT_FAULT_STATS * nr_node_ids; i++)
			grp->faults[i] = p->numa_faults[i];

		grp->total_faults = p->total_numa_faults;

		grp->nr_tasks++;
		rcu_assign_pointer(p->numa_group, grp);
	}

	rcu_read_lock();
	tsk = ACCESS_ONCE(cpu_rq(cpu)->curr);

	if (!cpupid_match_pid(tsk, cpupid))
		goto no_join;

	grp = rcu_dereference(tsk->numa_group);
	if (!grp)
		goto no_join;

	my_grp = p->numa_group;
	if (grp == my_grp)
		goto no_join;

	/*
	 * Only join the other group if its bigger; if we're the bigger group,
	 * the other task will join us.
	 */
	if (my_grp->nr_tasks > grp->nr_tasks)
		goto no_join;

	/*
	 * Tie-break on the grp address.
	 */
	if (my_grp->nr_tasks == grp->nr_tasks && my_grp > grp)
		goto no_join;

	/* Always join threads in the same process. */
	if (tsk->mm == current->mm)
		join = true;

	/* Simple filter to avoid false positives due to PID collisions */
	if (flags & TNF_SHARED)
		join = true;

	/* Update priv based on whether false sharing was detected */
	*priv = !join;

	if (join && !get_numa_group(grp))
		goto no_join;

	rcu_read_unlock();

	if (!join)
		return;

	BUG_ON(irqs_disabled());
	double_lock_irq(&my_grp->lock, &grp->lock);

    /* 把指定任务的 numa_pte_faults 信息从原来的 numa 组中更新到新的 numa 组中 */
	for (i = 0; i < NR_NUMA_HINT_FAULT_STATS * nr_node_ids; i++) {
		my_grp->faults[i] -= p->numa_faults[i];
		grp->faults[i] += p->numa_faults[i];
	}
	my_grp->total_faults -= p->total_numa_faults;
	grp->total_faults += p->total_numa_faults;

	my_grp->nr_tasks--;
	grp->nr_tasks++;

	spin_unlock(&my_grp->lock);
	spin_unlock_irq(&grp->lock);

	rcu_assign_pointer(p->numa_group, grp);

	put_numa_group(my_grp);
	return;

no_join:
	rcu_read_unlock();
	return;
}

/*********************************************************************************************************
** 函数名称: task_numa_free
** 功能描述: 释放并更新指定的任务和 numa 相关数据并释放其占用的 numa 资源
** 输	 入: p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void task_numa_free(struct task_struct *p)
{
	struct numa_group *grp = p->numa_group;
	void *numa_faults = p->numa_faults;
	unsigned long flags;
	int i;

	if (grp) {
		spin_lock_irqsave(&grp->lock, flags);
		for (i = 0; i < NR_NUMA_HINT_FAULT_STATS * nr_node_ids; i++)
			grp->faults[i] -= p->numa_faults[i];
		grp->total_faults -= p->total_numa_faults;

		grp->nr_tasks--;
		spin_unlock_irqrestore(&grp->lock, flags);
		RCU_INIT_POINTER(p->numa_group, NULL);
		put_numa_group(grp);
	}

	p->numa_faults = NULL;
	kfree(numa_faults);
}

/*
 * Got a PROT_NONE fault for a page on @node.
 */
/*********************************************************************************************************
** 函数名称: task_numa_fault
** 功能描述: 根据当前 cpu 正在执行的任务的 numa pte faults 数据通过任务迁移来提供系统性能
** 输	 入: last_cpupid - 上次触发 numa pte faults 进程的 page->_last_cpupid 字段值
**         : mem_node - 指定物理内存页所在 node id
**         : pages - 当前 numa pte faults 占用的物理内存页数
**         : flags - 和 numa pte faults 相关的标志，例如 TNF_MIGRATED
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void task_numa_fault(int last_cpupid, int mem_node, int pages, int flags)
{
	struct task_struct *p = current;
	bool migrated = flags & TNF_MIGRATED;
	int cpu_node = task_node(current);
	int local = !!(flags & TNF_FAULT_LOCAL);
	int priv;

	if (!numabalancing_enabled)
		return;

	/* for example, ksmd faulting in a user's mm */
	if (!p->mm)
		return;

	/* Allocate buffer to track faults on a per-node basis */
	if (unlikely(!p->numa_faults)) {
		int size = sizeof(*p->numa_faults) *
			   NR_NUMA_HINT_FAULT_BUCKETS * nr_node_ids;

		p->numa_faults = kzalloc(size, GFP_KERNEL|__GFP_NOWARN);
		if (!p->numa_faults)
			return;

		p->total_numa_faults = 0;
		memset(p->numa_faults_locality, 0, sizeof(p->numa_faults_locality));
	}

	/*
	 * First accesses are treated as private, otherwise consider accesses
	 * to be private if the accessing pid has not changed
	 */
	/* 1. 如果本次访问的内存是第一次被访问到，则设置为私有访问
	   2. 如果本次访问和上一次访问这个内存的进程的 pid 相同，也设置为私有访问 */
	if (unlikely(last_cpupid == (-1 & LAST_CPUPID_MASK))) {
		priv = 1;
	} else {
		priv = cpupid_match_pid(p, last_cpupid);
		if (!priv && !(flags & TNF_NO_GROUP))
			task_numa_group(p, last_cpupid, flags, &priv);
	}

	/*
	 * If a workload spans multiple NUMA nodes, a shared fault that
	 * occurs wholly within the set of nodes that the workload is
	 * actively using should be counted as local. This allows the
	 * scan rate to slow down when a workload has settled down.
	 */
	if (!priv && !local && p->numa_group &&
			node_isset(cpu_node, p->numa_group->active_nodes) &&
			node_isset(mem_node, p->numa_group->active_nodes))
		local = 1;

	task_numa_placement(p);

	/*
	 * Retry task to preferred node migration periodically, in case it
	 * case it previously failed, or the scheduler moved us.
	 */
	if (time_after(jiffies, p->numa_migrate_retry))
		numa_migrate_preferred(p);

	if (migrated)
		p->numa_pages_migrated += pages;
	if (flags & TNF_MIGRATE_FAIL)
		p->numa_faults_locality[2] += pages;

    /* 把发生了 numa_pte faults 的物理内存页数统计信息分别存储到 MEMBUF 和 CPUBUF 位置处 */
	p->numa_faults[task_faults_idx(NUMA_MEMBUF, mem_node, priv)] += pages;
	p->numa_faults[task_faults_idx(NUMA_CPUBUF, cpu_node, priv)] += pages;
	p->numa_faults_locality[local] += pages;
}

/*********************************************************************************************************
** 函数名称: reset_ptenuma_scan
** 功能描述: 把指定的任务和 numa 扫描相关的标志数据进行复位
** 输	 入: p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void reset_ptenuma_scan(struct task_struct *p)
{
	ACCESS_ONCE(p->mm->numa_scan_seq)++;
	p->mm->numa_scan_offset = 0;
}

/*
 * The expensive part of numa migration is done from task_work context.
 * Triggered from task_tick_numa().
 */
/*********************************************************************************************************
** 函数名称: task_numa_work
** 功能描述: 尝试对当前 cpu 上正在运行的进程执行 numa 扫描操作并更新和 numa 相关的变量信息
** 注     释: 我们通过在指定的周期内触发 numa_pte faults 就可以统计出在这个周期内进程访问的内存都在
**         : 什么位置，哪些 node 上，这样我们就可以知道每个进程使用的内存布局情况了，这样我们就可以
**         : 根据内存使用布局情况执行任务迁移操作来提高系统性能了
** 输	 入: p - 指定的工作结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void task_numa_work(struct callback_head *work)
{
	unsigned long migrate, next_scan, now = jiffies;
	struct task_struct *p = current;
	struct mm_struct *mm = p->mm;
	struct vm_area_struct *vma;
	unsigned long start, end;
	unsigned long nr_pte_updates = 0;
	long pages;

	WARN_ON_ONCE(p != container_of(work, struct task_struct, numa_work));

	work->next = work; /* protect against double add */
	/*
	 * Who cares about NUMA placement when they're dying.
	 *
	 * NOTE: make sure not to dereference p->mm before this check,
	 * exit_task_work() happens _after_ exit_mm() so we could be called
	 * without p->mm even though we still had it when we enqueued this
	 * work.
	 */
	if (p->flags & PF_EXITING)
		return;

	/* 设置当前正在运行的任务的下一次 numa 扫描时间 */
	if (!mm->numa_next_scan) {
		mm->numa_next_scan = now +
			msecs_to_jiffies(sysctl_numa_balancing_scan_delay);
	}

	/*
	 * Enforce maximal scan/migration frequency..
	 */
	/* 如果当前系统时间还没有达到 numa 扫描时间点则直接返回 */
	migrate = mm->numa_next_scan;
	if (time_before(now, migrate))
		return;

    /* 设置当前正在运行的任务的 numa 扫描周期相关数据 */
	if (p->numa_scan_period == 0) {
		p->numa_scan_period_max = task_scan_max(p);
		p->numa_scan_period = task_scan_min(p);
	}

    /* 尝试设置新的下一次 numa 扫描时间并判断是否需要执行 numa 扫描操作
       如果不需要则直接返回 */
	next_scan = now + msecs_to_jiffies(p->numa_scan_period);
	if (cmpxchg(&mm->numa_next_scan, migrate, next_scan) != migrate)
		return;

	/*
	 * Delay this task enough that another task of this mm will likely win
	 * the next time around.
	 */
	p->node_stamp += 2 * TICK_NSEC;

	start = mm->numa_scan_offset;

	/* 计算一次 numa 扫描周期内需要扫描的物理内存页个数 */
	pages = sysctl_numa_balancing_scan_size;
	pages <<= 20 - PAGE_SHIFT; /* MB in pages */
	if (!pages)
		return;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, start);
	if (!vma) {
		reset_ptenuma_scan(p);
		start = 0;
		vma = mm->mmap;
	}
	
	for (; vma; vma = vma->vm_next) {

	    /* 判断当前的 vma 结构是否可以迁移到其他 node 节点上 */
		if (!vma_migratable(vma) || !vma_policy_mof(vma) ||
			is_vm_hugetlb_page(vma)) {
			continue;
		}

		/*
		 * Shared library pages mapped by multiple processes are not
		 * migrated as it is expected they are cache replicated. Avoid
		 * hinting faults in read-only file-backed mappings or the vdso
		 * as migrating the pages will be of marginal benefit.
		 */
		/* 如果当前的 vma 映射的是共享库文件被多个进程共享，则不执行页面迁移操作
		   因为即使执行迁移操作也不会带来什么性能提升 */
		if (!vma->vm_mm ||
		    (vma->vm_file && (vma->vm_flags & (VM_READ|VM_WRITE)) == (VM_READ)))
			continue;

		/*
		 * Skip inaccessible VMAs to avoid any confusion between
		 * PROT_NONE and NUMA hinting ptes
		 */
		/* 如果指定的 vma 内存页面不具备访问属性则不执行迁移操作，这样可以避免
		   造成 PROT_NONE 和 numa_pte faults 之间的混淆 */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			continue;

        /* 把当前遍历的 vma 结构中和指定的虚拟地址范围匹配的地址空间属性设置为 PROT_NONE */
		do {
			start = max(start, vma->vm_start);
			end = ALIGN(start + (pages << PAGE_SHIFT), HPAGE_SIZE);
			end = min(end, vma->vm_end);
			nr_pte_updates += change_prot_numa(vma, start, end);

			/*
			 * Scan sysctl_numa_balancing_scan_size but ensure that
			 * at least one PTE is updated so that unused virtual
			 * address space is quickly skipped.
			 */
			if (nr_pte_updates)
				pages -= (end - start) >> PAGE_SHIFT;

			start = end;
			if (pages <= 0)
				goto out;

			cond_resched();
		} while (end != vma->vm_end);
	}

out:
	/*
	 * It is possible to reach the end of the VMA list but the last few
	 * VMAs are not guaranteed to the vma_migratable. If they are not, we
	 * would find the !migratable VMA on the next scan but not reset the
	 * scanner to the start so check it now.
	 */
	if (vma)
		mm->numa_scan_offset = start;
	else
		reset_ptenuma_scan(p);
	up_read(&mm->mmap_sem);
}

/*
 * Drive the periodic memory faults..
 */
/*********************************************************************************************************
** 函数名称: task_tick_numa
** 功能描述: 检查指定的任务的 numa 扫描周期时间是否已经到达，如果已经到达了则执行相关的扫描操作
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : curr - 需要检查的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void task_tick_numa(struct rq *rq, struct task_struct *curr)
{
	struct callback_head *work = &curr->numa_work;
	u64 period, now;

	/*
	 * We don't care about NUMA placement if we don't have memory.
	 */
	if (!curr->mm || (curr->flags & PF_EXITING) || work->next != work)
		return;

	/*
	 * Using runtime rather than walltime has the dual advantage that
	 * we (mostly) drive the selection from busy threads and that the
	 * task needs to have done some actual work before we bother with
	 * NUMA placement.
	 */
	now = curr->se.sum_exec_runtime;
	period = (u64)curr->numa_scan_period * NSEC_PER_MSEC;

	if (now - curr->node_stamp > period) {
		
		if (!curr->node_stamp)
			curr->numa_scan_period = task_scan_min(curr);
		
		curr->node_stamp += period;

        /* 如果到达了当前任务的 numa 扫描时间点则向系统内添加一个 task_numa_work 工作准备执行 */
		if (!time_before(jiffies, curr->mm->numa_next_scan)) {
			init_task_work(work, task_numa_work); /* TODO: move this into sched_fork() */
			task_work_add(curr, work, true);
		}
	}
}
#else
/*********************************************************************************************************
** 函数名称: task_tick_numa
** 功能描述: 检查指定的任务的 numa 扫描周期时间是否已经到达，如果已经到达了则执行相关的扫描操作
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : curr - 需要检查的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_tick_numa(struct rq *rq, struct task_struct *curr)
{
}

/*********************************************************************************************************
** 函数名称: account_numa_enqueue
** 功能描述: 在任务入队列时根据指定任务的 numa 数据更新指定的 cpu 运行队列中和 numa 相关的统计信息
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void account_numa_enqueue(struct rq *rq, struct task_struct *p)
{
}

/*********************************************************************************************************
** 函数名称: account_numa_enqueue
** 功能描述: 在任务出队列时根据指定任务的 numa 数据更新指定的 cpu 运行队列中和 numa 相关的统计信息
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void account_numa_dequeue(struct rq *rq, struct task_struct *p)
{
}
#endif /* CONFIG_NUMA_BALANCING */

/*********************************************************************************************************
** 函数名称: account_entity_enqueue
** 功能描述: 在向指定的 cfs 运行队列中添加指定的调度实例时用来更新调度相关的参数
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
account_entity_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	
	/* 如果当前调度实例的任务组父节点为 NULL，则表示这个调度实例直接挂载在 cpu 运行队列
	   上，所以我们需要同时更新 cpu 运行队列的权重信息 */
	if (!parent_entity(se))
		update_load_add(&rq_of(cfs_rq)->load, se->load.weight);
	
#ifdef CONFIG_SMP
	if (entity_is_task(se)) {
		struct rq *rq = rq_of(cfs_rq);

		account_numa_enqueue(rq, task_of(se));
		list_add(&se->group_node, &rq->cfs_tasks);
	}
#endif
	cfs_rq->nr_running++;
}

/*********************************************************************************************************
** 函数名称: account_entity_dequeue
** 功能描述: 在把指定的调度实例从指定的 cfs 运行队列中移除时用来更新调度相关的参数
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
account_entity_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);

	/* 如果当前调度实例的任务组父节点为 NULL，则表示这个调度实例直接挂载在 cpu 运行队列
	   上，所以我们需要同时更新 cpu 运行队列的权重信息 */
	if (!parent_entity(se))
		update_load_sub(&rq_of(cfs_rq)->load, se->load.weight);
	
	if (entity_is_task(se)) {
		account_numa_dequeue(rq_of(cfs_rq), task_of(se));
		list_del_init(&se->group_node);
	}
	cfs_rq->nr_running--;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
# ifdef CONFIG_SMP
/*********************************************************************************************************
** 函数名称: calc_tg_weight
** 功能描述: 计算指定任务组在其拥有的某个 cfs 运行队列上的负载权重值
** 输	 入: tg - 指定的任务组指针
**         : cfs_rq - 指定的任务组拥有的 cfs 运行队列指针
** 输	 出: tg_weight - 总的负载权重值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline long calc_tg_weight(struct task_group *tg, struct cfs_rq *cfs_rq)
{
	long tg_weight;

	/*
	 * Use this CPU's actual weight instead of the last load_contribution
	 * to gain a more accurate current total weight. See
	 * update_cfs_rq_load_contribution ().
	 */

	/* 使用指定任务组所属 cpu 的实际负载权重而不是最后一次的负载贡献值来
	   获得指定任务组更精确的总的负载权重信息 */
	tg_weight = atomic_long_read(&tg->load_avg);
	tg_weight -= cfs_rq->tg_load_contrib;
	tg_weight += cfs_rq->load.weight;

	return tg_weight;
}

/*********************************************************************************************************
** 函数名称: calc_cfs_shares
** 功能描述: 计算指定任务组拥有的某个 cfs 运行队列的 cfs shares 值
** 注     释: cfs shares - 表示任务组中的某个 cfs 运行队列占任务组权重的比例值
** 输	 入: cfs_rq - 指定的任务组拥有的 cfs 运行队列指针
**         : tg - 指定的任务组指针
** 输	 出: shares - cfs 运行队列的 shares 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static long calc_cfs_shares(struct cfs_rq *cfs_rq, struct task_group *tg)
{
	long tg_weight, load, shares;

	tg_weight = calc_tg_weight(tg, cfs_rq);
	load = cfs_rq->load.weight;

	shares = (tg->shares * load);

	/*              cfs_rq->load.weight
	   cfs shares = ------------------- * tg->shares
	                    tg_weight    */
	if (tg_weight)
		shares /= tg_weight;

	if (shares < MIN_SHARES)
		shares = MIN_SHARES;
	if (shares > tg->shares)
		shares = tg->shares;

	return shares;
}
# else /* CONFIG_SMP */
/*********************************************************************************************************
** 函数名称: calc_cfs_shares
** 功能描述: 计算指定任务组拥有的某个 cfs 运行队列的 shares 值
** 注     释: cfs shares - 表示任务组中的某个 cfs 运行队列占任务组权重的比例值
** 输	 入: cfs_rq - 指定的任务组拥有的 cfs 运行队列指针
**         : tg - 指定的任务组指针
** 输	 出: shares - cfs 运行队列的 shares 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline long calc_cfs_shares(struct cfs_rq *cfs_rq, struct task_group *tg)
{
	return tg->shares;
}
# endif /* CONFIG_SMP */

/*********************************************************************************************************
** 函数名称: reweight_entity
** 功能描述: 设置指定的 cfs 运行队列上指定的调度实例的权重为指定的值并更新所属运行队列的权重信息
** 输	 入: cfs_rq - 指定的任务所属 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : weight - 指定的新权重信息
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void reweight_entity(struct cfs_rq *cfs_rq, struct sched_entity *se,
			    unsigned long weight)
{
	if (se->on_rq) {
		/* commit outstanding execution time */
		if (cfs_rq->curr == se)
			update_curr(cfs_rq);
		account_entity_dequeue(cfs_rq, se);
	}

	update_load_set(&se->load, weight);

	if (se->on_rq)
		account_entity_enqueue(cfs_rq, se);
}

static inline int throttled_hierarchy(struct cfs_rq *cfs_rq);

/*********************************************************************************************************
** 函数名称: update_cfs_shares
** 功能描述: 更新指定任务组拥有的指定 cfs 运行队列在其所属任务组中的 shares 字段值
**         : cfs shares - 表示任务组中的某个 cfs 运行队列占任务组权重的比例值
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_cfs_shares(struct cfs_rq *cfs_rq)
{
	struct task_group *tg;
	struct sched_entity *se;
	long shares;

	tg = cfs_rq->tg;
	se = tg->se[cpu_of(rq_of(cfs_rq))];
	if (!se || throttled_hierarchy(cfs_rq))
		return;
#ifndef CONFIG_SMP
	if (likely(se->load.weight == tg->shares))
		return;
#endif
	shares = calc_cfs_shares(cfs_rq, tg);

	reweight_entity(cfs_rq_of(se), se, shares);
}
#else /* CONFIG_FAIR_GROUP_SCHED */
/*********************************************************************************************************
** 函数名称: update_cfs_shares
** 功能描述: 更新指定的 cfs 运行队列所属任务组的 tg->shares 字段值
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_cfs_shares(struct cfs_rq *cfs_rq)
{
}
#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_SMP
/*
 * We choose a half-life close to 1 scheduling period.
 * Note: The tables below are dependent on this value.
 */
#define LOAD_AVG_PERIOD 32
#define LOAD_AVG_MAX 47742 /* maximum possible load avg */
#define LOAD_AVG_MAX_N 345 /* number of full periods to produce LOAD_MAX_AVG */

/* Precomputed fixed inverse multiplies for multiplication by y^n */
/* 表示调度实例对系统的负载贡献在不同的衰减阶数下对应的衰减系数值，通过事先
   计算好可以加快程序运行效率 */
static const u32 runnable_avg_yN_inv[] = {
	0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6,
	0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
	0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581,
	0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
	0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80,
	0x85aac367, 0x82cd8698,
};

/*
 * Precomputed \Sum y^k { 1<=k<=n }.  These are floor(true_value) to prevent
 * over-estimates when re-combining.
 */
/* 计算指定个数的平均负载统计运行周期对应的平均负载贡献值，数组下标表示的是
   负载统计运行周期个数 */
static const u32 runnable_avg_yN_sum[] = {
	    0, 1002, 1982, 2941, 3880, 4798, 5697, 6576, 7437, 8279, 9103,
	 9909,10698,11470,12226,12966,13690,14398,15091,15769,16433,17082,
	17718,18340,18949,19545,20128,20698,21256,21802,22336,22859,23371,
};

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
/*********************************************************************************************************
** 函数名称: decay_load
** 功能描述: 对指定的权重数值进行指定阶数的衰减计算 ret = value * y ^ n ( y^32 ~= 0.5 )
** 输	 入: val - 指定的权重数值
**         : n - 指定的衰减阶数
** 输	 出: u64 - 衰减后的结果
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

	if (!n)
		return val;
	else if (unlikely(n > LOAD_AVG_PERIOD * 63))
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}

	val *= runnable_avg_yN_inv[local_n];
	/* We don't use SRR here since we always want to round down. */
	return val >> 32;
}

/*
 * For updates fully spanning n periods, the contribution to runnable
 * average will be: \Sum 1024*y^n
 *
 * We can compute this reasonably efficiently by combining:
 *   y^PERIOD = 1/2 with precomputed \Sum 1024*y^n {for  n <PERIOD}
 */
/*********************************************************************************************************
** 函数名称: __compute_runnable_contrib
** 功能描述: 计算指定个数的负载统计运行周期对应的可运行状态时间的负载贡献值
** 输	 入: n - 指定的负载统计运行周期个数
** 输	 出: u32 - 对应的平均负载贡献值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u32 __compute_runnable_contrib(u64 n)
{
	u32 contrib = 0;

	if (likely(n <= LOAD_AVG_PERIOD))
		return runnable_avg_yN_sum[n];
	else if (unlikely(n >= LOAD_AVG_MAX_N))
		return LOAD_AVG_MAX;

	/* Compute \Sum k^n combining precomputed values for k^i, \Sum k^j */
	do {
		contrib /= 2; /* y^LOAD_AVG_PERIOD = 1/2 */
		contrib += runnable_avg_yN_sum[LOAD_AVG_PERIOD];

		n -= LOAD_AVG_PERIOD;
	} while (n > LOAD_AVG_PERIOD);

	contrib = decay_load(contrib, n);
	return contrib + runnable_avg_yN_sum[n];
}

/*
 * We can represent the historical contribution to runnable average as the
 * coefficients of a geometric series.  To do this we sub-divide our runnable
 * history into segments of approximately 1ms (1024us); label the segment that
 * occurred N-ms ago p_N, with p_0 corresponding to the current period, e.g.
 *
 * [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
 *      p0            p1           p2
 *     (now)       (~1ms ago)  (~2ms ago)
 *
 * Let u_i denote the fraction of p_i that the entity was runnable.
 *
 * We then designate the fractions u_i as our co-efficients, yielding the
 * following representation of historical load:
 *   u_0 + u_1*y + u_2*y^2 + u_3*y^3 + ...
 *
 * We choose y based on the with of a reasonably scheduling period, fixing:
 *   y^32 = 0.5
 *
 * This means that the contribution to load ~32ms ago (u_32) will be weighted
 * approximately half as much as the contribution to load within the last ms
 * (u_0).
 *
 * When a period "rolls over" and we have new u_0`, multiplying the previous
 * sum again by y is sufficient to update:
 *   load_avg = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
 *            = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}]
 */
/*********************************************************************************************************
** 函数名称: __update_entity_runnable_avg
** 功能描述: 通过指定的当前系统时间更新指定实例的负载贡献统计结构的 runnable_avg_(sum/period) 字段值
** 输	 入: now - 指定的当前任务运行时钟时间，单位是 ns
**         : sa - 指定的负载贡献统计结构指针
**         : runnable - 表示指定的调度实例是否处于可运行状态
** 输	 出: decayed - 本次负载对应的时间是否达到一个负载统计周期
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline int __update_entity_runnable_avg(u64 now,
							struct sched_avg *sa,
							int runnable)
{
	u64 delta, periods;
	u32 runnable_contrib;
	int delta_w, decayed = 0;

	delta = now - sa->last_runnable_update;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_runnable_update = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10;
	if (!delta)
		return 0;
	sa->last_runnable_update = now;

	/* delta_w is the amount already accumulated against our next period */
	delta_w = sa->runnable_avg_period % 1024;

	/* delta + delta_w >= 1024 表示累计统计时间达到了一个负载统计周期
	   我们根据指定实例实际运行的统计周期个数来更新它的负载统计贡献值
	   如果累计统计时间不足一个负载统计周期，则直接累加统计时间即可 */
	if (delta + delta_w >= 1024) {
		/* period roll-over */
		decayed = 1;

		/*
		 * Now that we know we're crossing a period boundary, figure
		 * out how much from delta we need to complete the current
		 * period and accrue it.
		 */
		delta_w = 1024 - delta_w;
		if (runnable)
			sa->runnable_avg_sum += delta_w;
		sa->runnable_avg_period += delta_w;

		delta -= delta_w;

		/* Figure out how many additional periods this update spans */
		periods = delta / 1024;
		delta %= 1024;

		sa->runnable_avg_sum = decay_load(sa->runnable_avg_sum,
						  periods + 1);
		sa->runnable_avg_period = decay_load(sa->runnable_avg_period,
						     periods + 1);

		/* Efficiently calculate \sum (1..n_period) 1024*y^i */
		runnable_contrib = __compute_runnable_contrib(periods);
		if (runnable)
			sa->runnable_avg_sum += runnable_contrib;
		sa->runnable_avg_period += runnable_contrib;
	}

	/* Remainder of delta accrued against u_0` */
	if (runnable)
		sa->runnable_avg_sum += delta;
	sa->runnable_avg_period += delta;

	return decayed;
}

/* Synchronize an entity's decay with its parenting cfs_rq.*/
/*********************************************************************************************************
** 函数名称: __synchronize_entity_decay
** 功能描述: 把指定的调度实例的负载贡献衰减到和所属 cfs 运行队列相同的衰减阶数
** 输	 入: se - 指定的调度实例指针
** 输	 出: decays - 本次新衰减的阶数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 __synchronize_entity_decay(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	u64 decays = atomic64_read(&cfs_rq->decay_counter);

    /* se->avg.decay_count 表示指定的调度实例负载已经衰减的阶数
	   cfs_rq->decay_counter 表示这个调度实例所属 cfs 运行队列负载需要衰减的阶数
	   decays -= se->avg.decay_count 表示指定的调度实例负载还需要再衰减的阶数 */
	decays -= se->avg.decay_count;
	
	se->avg.decay_count = 0;
	if (!decays)
		return 0;

    /* 把指定的调度实例负载贡献同步衰减到其所属 cfs 运行队列的负载衰减阶数 */
	se->avg.load_avg_contrib = decay_load(se->avg.load_avg_contrib, decays);

	return decays;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*********************************************************************************************************
** 函数名称: __update_cfs_rq_tg_load_contrib
** 功能描述: 更新指定任务组的 cfs 运行队列在过去“时间段”经过衰减且乘以权重信息的负载贡献值
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : force_update - 是否强制更新
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_cfs_rq_tg_load_contrib(struct cfs_rq *cfs_rq,
						 int force_update)
{
	struct task_group *tg = cfs_rq->tg;
	long tg_contrib;

    /* 计算当前任务组负载贡献统计值与上一次同步任务组负载贡献统计值的增量部分 */
	tg_contrib = cfs_rq->runnable_load_avg + cfs_rq->blocked_load_avg;

	/* 计算当前指定任务组的 cfs 运行队列的衰减加权贡献值的增量值 */
	tg_contrib -= cfs_rq->tg_load_contrib;

	if (!tg_contrib)
		return;

    /* (abs(tg_contrib) > cfs_rq->tg_load_contrib / 8) 表示只有在任务组负载贡献统计值的
	   增量达到了之前负载贡献的八分之一才会把这个增量部分同步到任务组负载贡献统计值中 */
	if (force_update || abs(tg_contrib) > cfs_rq->tg_load_contrib / 8) {
		atomic_long_add(tg_contrib, &tg->load_avg);
		cfs_rq->tg_load_contrib += tg_contrib;
	}
}

/*
 * Aggregate cfs_rq runnable averages into an equivalent task_group
 * representation for computing load contributions.
 */
/*********************************************************************************************************
** 函数名称: __update_task_entity_contrib
** 功能描述: 尝试更新任务组中指定的 cfs 运行队列对应的 sched_avg 结构的 runnable_avg 贡献值信息
** 注     释: 任务组在每一个 cpu 上都有一个调度实例和一个 cfs 运行队列，这个函数的 sa 表示在当前 cpu 
**         : 上调度实例的负载统计结构，cfs_rq 表示在当前 cpu 上的运行队列，sa 和 cfs 一一对应
** 输	 入: sa - 指定 cfs 运行队列的平均负载贡献数据结构指针
**         : cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_tg_runnable_avg(struct sched_avg *sa,
						  struct cfs_rq *cfs_rq)
{
	struct task_group *tg = cfs_rq->tg;
	long contrib;

	/* The fraction of a cpu used by this cfs_rq */
	/*
	 contrib = ((sa->runnable_avg_sum << NICE_0_SHIFT) / (sa->runnable_avg_period + 1)) - cfs_rq->tg_runnable_contrib 

	           sa->runnable_avg_sum << NICE_0_SHIFT
	         = ------------------------------------ - cfs_rq->tg_runnable_contrib
	               sa->runnable_avg_period + 1   */

	contrib = div_u64((u64)sa->runnable_avg_sum << NICE_0_SHIFT,
			  sa->runnable_avg_period + 1);


    /* 计算当前调度实例对于任务组负载贡献的增量值 */
	contrib -= cfs_rq->tg_runnable_contrib;

    /* 在当前调度实例对任务组负载贡献的增量值大于 cfs_rq->tg_runnable_contrib / 64 时将其统计到对应的任务组负载贡献中 */
	if (abs(contrib) > cfs_rq->tg_runnable_contrib / 64) {
	     /*                                     sa->runnable_avg_sum << NICE_0_SHIFT
	      tg->runnable_avg = tg->runnable_avg + ------------------------------------ - cfs_rq->tg_runnable_contrib
	                                                sa->runnable_avg_period + 1 
                                         
                                                sa->runnable_avg_sum_new << NICE_0_SHIFT   sa->runnable_avg_sum_old << NICE_0_SHIFT
	                       = tg->runnable_avg + ---------------------------------------- - ----------------------------------------                        
	                                                sa->runnable_avg_period_new + 1            sa->runnable_avg_period_old + 1   */
		 
	      /*														   sa->runnable_avg_sum << NICE_0_SHIFT
		   cfs_rq->tg_runnable_contrib = cfs_rq->tg_runnable_contrib + ------------------------------------ - cfs_rq->tg_runnable_contrib
																		   sa->runnable_avg_period + 1 
										 sa->runnable_avg_sum << NICE_0_SHIFT
									   = ------------------------------------
											 sa->runnable_avg_period + 1   */
		atomic_add(contrib, &tg->runnable_avg);
		cfs_rq->tg_runnable_contrib += contrib;
	}
}

/*********************************************************************************************************
** 函数名称: __update_task_entity_contrib
** 功能描述: 更新指定的调度任务组实例在过去“时间段”内经过衰减加权的负载贡献值
** 输	 入: se - 指定的调度任务组实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_group_entity_contrib(struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = group_cfs_rq(se);
	struct task_group *tg = cfs_rq->tg;
	int runnable_avg;
	u64 contrib;

    /* se->avg.load_avg_contrib = cfs_rq->tg_load_contrib * tg->shares / (tg->load_avg + 1)

                                                               tg->shares
                                = cfs_rq->tg_load_contrib * ----------------
                                                            tg->load_avg + 1   */
	contrib = cfs_rq->tg_load_contrib * tg->shares;

	/* 计算到这里 se->avg.load_avg_contrib 表示的是任务组在当前 cpu 上的 cfs 运行队列贡献
	   的负载量占整个任务组负载贡献量的比例值，相当于当前 cpu 运行队列在任务组中的权重信息 */
	se->avg.load_avg_contrib = div_u64(contrib,
				     atomic_long_read(&tg->load_avg) + 1);
	
	/*
	 * For group entities we need to compute a correction term in the case
	 * that they are consuming <1 cpu so that we would contribute the same
	 * load as a task of equal weight.
	 *
	 * Explicitly co-ordinating this measurement would be expensive, but
	 * fortunately the sum of each cpus contribution forms a usable
	 * lower-bound on the true value.
	 *
	 * Consider the aggregate of 2 contributions.  Either they are disjoint
	 * (and the sum represents true value) or they are disjoint and we are
	 * understating by the aggregate of their overlap.
	 *
	 * Extending this to N cpus, for a given overlap, the maximum amount we
	 * understand is then n_i(n_i+1)/2 * w_i where n_i is the number of
	 * cpus that overlap for this interval and w_i is the interval width.
	 *
	 * On a small machine; the first term is well-bounded which bounds the
	 * total error since w_i is a subset of the period.  Whereas on a
	 * larger machine, while this first term can be larger, if w_i is the
	 * of consequential size guaranteed to see n_i*w_i quickly converge to
	 * our upper bound of 1-cpu.
	 */
	runnable_avg = atomic_read(&tg->runnable_avg);

	/* 为了避免 se->avg.runnable_avg_sum >= se->avg.runnable_avg_period 的情况 */
	if (runnable_avg < NICE_0_LOAD) {
		/*                                              cfs_rq->tg_load_contrib * tg->shares
          se->avg.load_avg_contrib = tg->runnable_avg * ------------------------------------
	                                                            tg->load_avg + 1          */

		se->avg.load_avg_contrib *= runnable_avg;
		se->avg.load_avg_contrib >>= NICE_0_SHIFT;
	}
}

/*********************************************************************************************************
** 函数名称: update_rq_runnable_avg
** 功能描述: 更新指定的 cpu 运行队列的 runnable_avg 贡献值信息，包括任务和任务组的
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : runnable - 表示指定的调度实例是否在运行
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_rq_runnable_avg(struct rq *rq, int runnable)
{
	__update_entity_runnable_avg(rq_clock_task(rq), &rq->avg, runnable);
	__update_tg_runnable_avg(&rq->avg, &rq->cfs);
}
#else /* CONFIG_FAIR_GROUP_SCHED */

/*********************************************************************************************************
** 函数名称: __update_cfs_rq_tg_load_contrib
** 功能描述: 更新指定的 cfs 运行队列的调度任务组对系统综合平均负载贡献值信息
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : force_update - 是否强制更新
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_cfs_rq_tg_load_contrib(struct cfs_rq *cfs_rq,
						 int force_update) {}

/*********************************************************************************************************
** 函数名称: __update_task_entity_contrib
** 功能描述: 尝试更新指定的 cfs 运行队列以及所属任务组的 runnable_avg 贡献值信息
** 输	 入: sa - 指定的平均负载贡献数据结构指针
**         : cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_tg_runnable_avg(struct sched_avg *sa,
						  struct cfs_rq *cfs_rq) {}

/*********************************************************************************************************
** 函数名称: __update_task_entity_contrib
** 功能描述: 更新指定的调度任务组实例在过去“时间段”内经过衰减加权的负载贡献值
** 输	 入: se - 指定的调度任务组实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_group_entity_contrib(struct sched_entity *se) {}

/*********************************************************************************************************
** 函数名称: update_rq_runnable_avg
** 功能描述: 更新指定的 cpu 运行队列的 runnable_avg 贡献值信息，包括任务和任务组的
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : runnable - 表示指定的调度实例是否在运行
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_rq_runnable_avg(struct rq *rq, int runnable) {}
#endif /* CONFIG_FAIR_GROUP_SCHED */

/*********************************************************************************************************
** 函数名称: __update_task_entity_contrib
** 功能描述: 更新指定的任务实例在过去“时间段”内经过衰减加权的负载贡献值
** 输	 入: se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __update_task_entity_contrib(struct sched_entity *se)
{
	u32 contrib;

	/* avoid overflowing a 32-bit type w/ SCHED_LOAD_SCALE */
	/*                            se->avg.runnable_avg_sum * se->load.weight
	   se->avg.load_avg_contrib = ------------------------------------------
	                                   se->avg.runnable_avg_period + 1    */
	contrib = se->avg.runnable_avg_sum * scale_load_down(se->load.weight);
	contrib /= (se->avg.runnable_avg_period + 1);
	se->avg.load_avg_contrib = scale_load(contrib);
}

/* Compute the current contribution to load_avg by se, return any delta */
/*********************************************************************************************************
** 函数名称: __update_entity_load_avg_contrib
** 功能描述: 更新指定的调度实例在过去“时间段”内经过衰减加权后的负载贡献值
** 输	 入: se - 指定的调度实例指针
** 输	 出: long - 本次更新衰减加权负载贡献值的增量值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static long __update_entity_load_avg_contrib(struct sched_entity *se)
{
	long old_contrib = se->avg.load_avg_contrib;

	if (entity_is_task(se)) {
		__update_task_entity_contrib(se);
	} else {
	    /* 在统计任务组对系统平均负载贡献时的数据流图如下：
	       sched_avg->runnable_avg_sum << NICE_0_SHIFT
           -------------------------------------------   >>>>>>>>>   task_group->runnable_avg
                   sa->runnable_avg_period + 1

           task_group->runnable_avg
           ------------------------   >>>>>>>>>   sched_avg->avg.load_avg_contrib
               2 ^ NICE_0_SHIFT     

           详情见下面两个函数的计算过程 */
		__update_tg_runnable_avg(&se->avg, group_cfs_rq(se));
		__update_group_entity_contrib(se);
	}

	return se->avg.load_avg_contrib - old_contrib;
}

/*********************************************************************************************************
** 函数名称: subtract_blocked_load_contrib
** 功能描述: 从指定的 cfs 队列的 cfs_rq->blocked_load_avg 中减去指定的负载贡献值
** 输	 入: cfs_rq - 指定的调度实例指针
**         : load_contrib - 需要减去的负载贡献值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void subtract_blocked_load_contrib(struct cfs_rq *cfs_rq,
						 long load_contrib)
{
	if (likely(load_contrib < cfs_rq->blocked_load_avg))
		cfs_rq->blocked_load_avg -= load_contrib;
	else
		cfs_rq->blocked_load_avg = 0;
}

static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq);

/* Update a sched_entity's runnable average */
/*********************************************************************************************************
** 函数名称: update_entity_load_avg
** 功能描述: 更新指定的调度实例处于可运行状态时间对系统平均负载贡献值信息
** 输	 入: se - 指定的调度实例指针
**         : update_cfs_rq - 表示是否更新指定调度实例所属 cfs 运行队列的负载贡献值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_entity_load_avg(struct sched_entity *se,
					  int update_cfs_rq)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	long contrib_delta;
	u64 now;

	/*
	 * For a group entity we need to use their owned cfs_rq_clock_task() in
	 * case they are the parent of a throttled hierarchy.
	 */
	if (entity_is_task(se))
		now = cfs_rq_clock_task(cfs_rq);
	else
		now = cfs_rq_clock_task(group_cfs_rq(se));

	if (!__update_entity_runnable_avg(now, &se->avg, se->on_rq))
		return;

	contrib_delta = __update_entity_load_avg_contrib(se);

	if (!update_cfs_rq)
		return;

	if (se->on_rq)
		cfs_rq->runnable_load_avg += contrib_delta;
	else
		subtract_blocked_load_contrib(cfs_rq, -contrib_delta);
}

/*
 * Decay the load contributed by all blocked children and account this so that
 * their contribution may appropriately discounted when they wake up.
 */
/*********************************************************************************************************
** 函数名称: update_cfs_rq_blocked_load
** 功能描述: 更新指定的 cfs 运行队列的阻塞负载贡献值以及综合平均负载贡献值信息
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : force_update - 表示是否强制更新
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_cfs_rq_blocked_load(struct cfs_rq *cfs_rq, int force_update)
{
	u64 now = cfs_rq_clock_task(cfs_rq) >> 20;
	u64 decays;

	/* 计算本次对负载贡献的衰减系数
	   因为一个负载贡献统计周期是 1024us、即大约 1ms，所以我们只要计算从上一次衰减到本次衰减
	   之间消耗的时间然后再转换成 ms 单位就可以知道在这期间一共经历的周期数，即衰减阶数 */
	decays = now - cfs_rq->last_decay;
	if (!decays && !force_update)
		return;

    /* 从指定的 cfs 运行队列的 cfs_rq->blocked_load_avg 中移除 cfs_rq->removed_load 负载量 */
	if (atomic_long_read(&cfs_rq->removed_load)) {
		unsigned long removed_load;
		removed_load = atomic_long_xchg(&cfs_rq->removed_load, 0);
		subtract_blocked_load_contrib(cfs_rq, removed_load);
	}

	if (decays) {
		cfs_rq->blocked_load_avg = decay_load(cfs_rq->blocked_load_avg,
						      decays);
		atomic64_add(decays, &cfs_rq->decay_counter);
		cfs_rq->last_decay = now;
	}

	__update_cfs_rq_tg_load_contrib(cfs_rq, force_update);
}

/* Add the load generated by se into cfs_rq's child load-average */
/*********************************************************************************************************
** 函数名称: enqueue_entity_load_avg
** 功能描述: 向指定的 cfs 运行队列中添加指定的调度实例时用来同步负载贡献衰减阶数
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : wakeup - 表示是否包含 ENQUEUE_WAKEUP 标志
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void enqueue_entity_load_avg(struct cfs_rq *cfs_rq,
						  struct sched_entity *se,
						  int wakeup)
{
	/*
	 * We track migrations using entity decay_count <= 0, on a wake-up
	 * migration we use a negative decay count to track the remote decays
	 * accumulated while sleeping.
	 *
	 * Newly forked tasks are enqueued with se->avg.decay_count == 0, they
	 * are seen by enqueue_entity_load_avg() as a migration with an already
	 * constructed load_avg_contrib.
	 */
	if (unlikely(se->avg.decay_count <= 0)) {

	    /* 将新入队列调度实例的负载更新时钟和其所入 cfs 运行队列的任务时钟同步 */
		se->avg.last_runnable_update = rq_clock_task(rq_of(cfs_rq));

	    /* 如果 se->avg.decay_count 小于 0，则表示当前调度实例在任务迁移前，和其所属 cfs 运行队列任务时钟
	       相比，负载还需要再衰减的阶数（衰减后的负载是和所属 cfs 运行队列任务时钟同步的负载）*/
		if (se->avg.decay_count) {
			/*
			 * In a wake-up migration we have to approximate the
			 * time sleeping.  This is because we can't synchronize
			 * clock_task between the two cpus, and it is not
			 * guaranteed to be read-safe.  Instead, we can
			 * approximate this using our carried decays, which are
			 * explicitly atomically readable.
			 */
			se->avg.last_runnable_update -= (-se->avg.decay_count) << 20;
			update_entity_load_avg(se, 0);
			/* Indicate that we're now synchronized and on-rq */
			se->avg.decay_count = 0;
		}
		wakeup = 0;
	} else {
		__synchronize_entity_decay(se);
	}

	/* migrated tasks did not contribute to our blocked load */
	if (wakeup) {
		subtract_blocked_load_contrib(cfs_rq, se->avg.load_avg_contrib);
		update_entity_load_avg(se, 0);
	}

	cfs_rq->runnable_load_avg += se->avg.load_avg_contrib;
	/* we force update consideration on load-balancer moves */
	update_cfs_rq_blocked_load(cfs_rq, !wakeup);
}

/*
 * Remove se's load from this cfs_rq child load-average, if the entity is
 * transitioning to a blocked state we track its projected decay using
 * blocked_load_avg.
 */
/*********************************************************************************************************
** 函数名称: dequeue_entity_load_avg
** 功能描述: 从指定的 cfs 运行队列中移除指定的调度实例时用来更新负载贡献相关数据
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : sleep - 是否设置 DEQUEUE_SLEEP 标志
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void dequeue_entity_load_avg(struct cfs_rq *cfs_rq,
						  struct sched_entity *se,
						  int sleep)
{
	update_entity_load_avg(se, 1);
	/* we force update consideration on load-balancer moves */
	update_cfs_rq_blocked_load(cfs_rq, !sleep);

	cfs_rq->runnable_load_avg -= se->avg.load_avg_contrib;
	
	if (sleep) {
		cfs_rq->blocked_load_avg += se->avg.load_avg_contrib;
		se->avg.decay_count = atomic64_read(&cfs_rq->decay_counter);
	} /* migrations, e.g. sleep=0 leave decay_count == 0 */
}

/*
 * Update the rq's load with the elapsed running time before entering
 * idle. if the last scheduled task is not a CFS task, idle_enter will
 * be the only way to update the runnable statistic.
 */
/*********************************************************************************************************
** 函数名称: idle_enter_fair
** 功能描述: 在进入 idle 进程之前调用，用来更新指定的 cpu 运行队列的 runnable_avg 贡献值信息
** 输	 入: this_rq- 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void idle_enter_fair(struct rq *this_rq)
{
	update_rq_runnable_avg(this_rq, 1);
}

/*
 * Update the rq's load with the elapsed idle time before a task is
 * scheduled. if the newly scheduled task is not a CFS task, idle_exit will
 * be the only way to update the runnable statistic.
 */
/*********************************************************************************************************
** 函数名称: idle_exit_fair
** 功能描述: 在退出 idle 进程之前调用，用来更新指定的 cpu 运行队列的 runnable_avg 贡献值信息
** 输	 入: this_rq- 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void idle_exit_fair(struct rq *this_rq)
{
	update_rq_runnable_avg(this_rq, 0);
}

static int idle_balance(struct rq *this_rq);

#else /* CONFIG_SMP */

/*********************************************************************************************************
** 函数名称: update_entity_load_avg
** 功能描述: 更新指定的调度实例对系统平均负载贡献值信息
** 输	 入: se - 指定的调度实例指针
**         : update_cfs_rq - 表示是否更新指定调度实例所属 cfs 运行队列的负载贡献值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_entity_load_avg(struct sched_entity *se,
					  int update_cfs_rq) {}

/*********************************************************************************************************
** 函数名称: update_rq_runnable_avg
** 功能描述: 更新指定的 cpu 运行队列的 runnable_avg 贡献值信息，包括任务和任务组的
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : runnable - 表示指定的调度实例是否在运行
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_rq_runnable_avg(struct rq *rq, int runnable) {}

/*********************************************************************************************************
** 函数名称: enqueue_entity_load_avg
** 功能描述: 向指定的 cfs 运行队列中添加指定的调度实例时用来同步负载贡献衰减阶数
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : wakeup - 表示是否包含 ENQUEUE_WAKEUP 标志
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void enqueue_entity_load_avg(struct cfs_rq *cfs_rq,
					   struct sched_entity *se,
					   int wakeup) {}

/*********************************************************************************************************
** 函数名称: dequeue_entity_load_avg
** 功能描述: 从指定的 cfs 运行队列中移除指定的调度实例时用来更新负载贡献相关数据
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : sleep - 是否设置 DEQUEUE_SLEEP 标志
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void dequeue_entity_load_avg(struct cfs_rq *cfs_rq,
					   struct sched_entity *se,
					   int sleep) {}

/*********************************************************************************************************
** 函数名称: update_cfs_rq_blocked_load
** 功能描述: 更新指定的 cfs 运行队列的阻塞负载贡献值以及综合平均负载贡献值信息
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : force_update - 表示是否强制更新
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_cfs_rq_blocked_load(struct cfs_rq *cfs_rq,
					      int force_update) {}

static inline int idle_balance(struct rq *rq)
{
	return 0;
}

#endif /* CONFIG_SMP */

/*********************************************************************************************************
** 函数名称: enqueue_sleeper
** 功能描述: 在向指定的 cfs 调度队列中添加一个睡眠调度实例时调用，用来统计睡眠和阻塞相关时间数据
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHEDSTATS
	struct task_struct *tsk = NULL;

	if (entity_is_task(se))
		tsk = task_of(se);

	if (se->statistics.sleep_start) {
		u64 delta = rq_clock(rq_of(cfs_rq)) - se->statistics.sleep_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->statistics.sleep_max))
			se->statistics.sleep_max = delta;

		se->statistics.sleep_start = 0;
		se->statistics.sum_sleep_runtime += delta;

		if (tsk) {
			account_scheduler_latency(tsk, delta >> 10, 1);
			trace_sched_stat_sleep(tsk, delta);
		}
	}
	if (se->statistics.block_start) {
		u64 delta = rq_clock(rq_of(cfs_rq)) - se->statistics.block_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->statistics.block_max))
			se->statistics.block_max = delta;

		se->statistics.block_start = 0;
		se->statistics.sum_sleep_runtime += delta;

		if (tsk) {
			if (tsk->in_iowait) {
				se->statistics.iowait_sum += delta;
				se->statistics.iowait_count++;
				trace_sched_stat_iowait(tsk, delta);
			}

			trace_sched_stat_blocked(tsk, delta);

			/*
			 * Blocking time is in units of nanosecs, so shift by
			 * 20 to get a milliseconds-range estimation of the
			 * amount of time that the task spent sleeping:
			 */
			if (unlikely(prof_on == SLEEP_PROFILING)) {
				profile_hits(SLEEP_PROFILING,
						(void *)get_wchan(tsk),
						delta >> 20);
			}
			account_scheduler_latency(tsk, delta >> 10, 0);
		}
	}
#endif
}

/*********************************************************************************************************
** 函数名称: check_spread
** 功能描述: 
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void check_spread(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	s64 d = se->vruntime - cfs_rq->min_vruntime;

	if (d < 0)
		d = -d;

	if (d > 3*sysctl_sched_latency)
		schedstat_inc(cfs_rq, nr_spread_over);
#endif
}

/*********************************************************************************************************
** 函数名称: place_entity
** 功能描述: 更新/设置指定的 cfs 运行队列中指定的调度实例的虚拟时间信息
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : initial - 是否初始化新的调度实例的虚拟运行时间
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	/* 表示把指定的调度实例添加到当前 cpu 正在运行的任务的右侧（在 cfs 
	   运行队列的红黑树上的位置），这样不会影响当前正在运行的任务调度 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_vslice(cfs_rq, se);

	/* sleeps up to a single latency don't count. */	
	/* 表示把指定的调度实例添加到当前 cpu 正在运行的任务的左侧（在 cfs 
	   运行队列的红黑树上的位置），这样会在最短时间内抢占当前正在运行的任务 */
	if (!initial) {
		unsigned long thresh = sysctl_sched_latency;

		/*
		 * Halve their sleep time's effect, to allow
		 * for a gentler effect of sleepers:
		 */
		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;

		vruntime -= thresh;
	}

	/* ensure we never gain time by being placed backwards. */
	se->vruntime = max_vruntime(se->vruntime, vruntime);
}

static void check_enqueue_throttle(struct cfs_rq *cfs_rq);

/*********************************************************************************************************
** 函数名称: enqueue_entity
** 功能描述: 把指定的调度实例添加到指定的 cfs 运行队列上并更新相关数据
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : flags - ENQUEUE flags，例如 ENQUEUE_WAKEUP
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update the normalized vruntime before updating min_vruntime
	 * through calling update_curr().
	 */	
    /* 因为每个 cpu 都有自己的运行队列，每个队列中的进程的 vruntime 也走得有快有慢
       如果一个进程从 min_vruntime 更小的 cpu(A) 上迁移到 min_vruntime 更大的 cpu(B)
       上，可能就会占便宜了，因为 cpu(B) 的运行队列中进程的 vruntime 普遍比较大，迁
       移过来的进程就会获得更多的 cpu 时间片，为了处理不同 cpu 运行队列之间存在的虚拟
       运行时间差异，我们在一个任务 dequeue_entity 的时候会减去这个 cpu 运行队列的 
       min_vruntime，而在 enqueue_entity 的时候会再加上新的 cpu 运行队列的 min_vruntime */
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_WAKING))
		se->vruntime += cfs_rq->min_vruntime;

	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	enqueue_entity_load_avg(cfs_rq, se, flags & ENQUEUE_WAKEUP);
	account_entity_enqueue(cfs_rq, se);
	update_cfs_shares(cfs_rq);

	/* 表示把指定的调度实例添加到当前 cpu 正在运行的任务的左侧（在 cfs 
	   运行队列的红黑树上的位置），这样会在最短时间内抢占当前正在运行的任务 */
	if (flags & ENQUEUE_WAKEUP) {
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)
		__enqueue_entity(cfs_rq, se);
	se->on_rq = 1;

	if (cfs_rq->nr_running == 1) {
		list_add_leaf_cfs_rq(cfs_rq);
		check_enqueue_throttle(cfs_rq);
	}
}

/*********************************************************************************************************
** 函数名称: __clear_buddies_last
** 功能描述: 清除指定任务组所在任务组树形结构中、这个任务组到树形结构根节点路径的所有 cfs 运行队列的
**         : cfs_rq->last 字段指定自己的 cfs 运行队列的 cfs_rq->last 字段值
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __clear_buddies_last(struct sched_entity *se)
{
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		if (cfs_rq->last != se)
			break;

		cfs_rq->last = NULL;
	}
}

/*********************************************************************************************************
** 函数名称: __clear_buddies_next
** 功能描述: 清除指定任务组所在任务组树形结构中、这个任务组到树形结构根节点路径的所有 cfs 运行队列的
**         : cfs_rq->next 字段指定自己的 cfs 运行队列的 cfs_rq->next 字段值
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __clear_buddies_next(struct sched_entity *se)
{
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		if (cfs_rq->next != se)
			break;

		cfs_rq->next = NULL;
	}
}

/*********************************************************************************************************
** 函数名称: __clear_buddies_skip
** 功能描述: 清除指定任务组所在任务组树形结构中、这个任务组到树形结构根节点路径的所有 cfs 运行队列的
**         : cfs_rq->skip 字段指定自己的 cfs 运行队列的 cfs_rq->skip 字段值
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __clear_buddies_skip(struct sched_entity *se)
{
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		if (cfs_rq->skip != se)
			break;

		cfs_rq->skip = NULL;
	}
}

/*********************************************************************************************************
** 函数名称: clear_buddies
** 功能描述: 尝试清除指定任务组所在任务组树形结构中、这个任务组到树形结构根节点路径的所有 cfs 运行
**         : 队列的 cfs_rq->last/next/skip 字段指定自己的 cfs 运行队列的 cfs_rq->last/next/skip 
**         : 字段值，这样指定的调度实例就不会在很短时间内再次被调度到了
** 输	 入: se - 指定的任务组调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->last == se)
		__clear_buddies_last(se);

	if (cfs_rq->next == se)
		__clear_buddies_next(se);

	if (cfs_rq->skip == se)
		__clear_buddies_skip(se);
}

static __always_inline void return_cfs_rq_runtime(struct cfs_rq *cfs_rq);

/*********************************************************************************************************
** 函数名称: dequeue_entity
** 功能描述: 把指定的调度实例从指定的 cfs 运行队列上移除并更新相关数据
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
**         : flags - DEQUEUE flags，例如 DEQUEUE_SLEEP
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	dequeue_entity_load_avg(cfs_rq, se, flags & DEQUEUE_SLEEP);
	update_stats_dequeue(cfs_rq, se);
	
	if (flags & DEQUEUE_SLEEP) {
#ifdef CONFIG_SCHEDSTATS
		if (entity_is_task(se)) {
			struct task_struct *tsk = task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->statistics.sleep_start = rq_clock(rq_of(cfs_rq));
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->statistics.block_start = rq_clock(rq_of(cfs_rq));
		}
#endif
	}

	clear_buddies(cfs_rq, se);

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	se->on_rq = 0;
	account_entity_dequeue(cfs_rq, se);

	/*
	 * Normalize the entity after updating the min_vruntime because the
	 * update can refer to the ->curr item and we need to reflect this
	 * movement in our normalized position.
	 */	
    /* 因为每个 cpu 都有自己的运行队列，每个队列中的进程的 vruntime 也走得有快有慢
       如果一个进程从 min_vruntime 更小的 cpu(A) 上迁移到 min_vruntime 更大的 cpu(B)
       上，可能就会占便宜了，因为 cpu(B) 的运行队列中进程的 vruntime 普遍比较大，迁
       移过来的进程就会获得更多的 cpu 时间片，为了处理不同 cpu 运行队列之间存在的虚拟
       运行时间差异，我们在一个任务 dequeue_entity 的时候会减去这个 cpu 运行队列的 
       min_vruntime，而在 enqueue_entity 的时候会再加上新的 cpu 运行队列的 min_vruntime */
	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= cfs_rq->min_vruntime;

	/* return excess runtime on last dequeue */
	return_cfs_rq_runtime(cfs_rq);

	update_min_vruntime(cfs_rq);
	update_cfs_shares(cfs_rq);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
/*********************************************************************************************************
** 函数名称: check_preempt_tick
** 功能描述: 检查指定的正在运行的调度实例是否已经运行了足够时间并被抢占，如果可以则在指定的 cpu 运行
**         : 队列上执行一次调度操作，判断是否可以抢占有两个条件，如下：
**         : 1. 如果当前正在执行的调度实例分配的物理时间片已经运行完，则表示可以被抢占 
**         : 2. 如果当前正在执行的调度实例运行的物理时间大于最小调度粒度且减去红黑树上虚拟运行时间最小的
**         :    调度实例的差值大于为当前调度实例分配的物理调度时间片，则表示可以被抢占
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : curr - 指定的当前 cpu 正在运行的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;
	struct sched_entity *se;
	s64 delta;

	ideal_runtime = sched_slice(cfs_rq, curr);
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	
    /* 根据物理运行时间判断当前正在执行的调度实例是否可以被抢占 */
	if (delta_exec > ideal_runtime) {
		resched_curr(rq_of(cfs_rq));
		/*
		 * The current task ran long enough, ensure it doesn't get
		 * re-elected due to buddy favours.
		 */
		clear_buddies(cfs_rq, curr);
		return;
	}

	/*
	 * Ensure that a task that missed wakeup preemption by a
	 * narrow margin doesn't have to wait for a full slice.
	 * This also mitigates buddy induced latencies under load.
	 */
	if (delta_exec < sysctl_sched_min_granularity)
		return;

	se = __pick_first_entity(cfs_rq);
	delta = curr->vruntime - se->vruntime;

	if (delta < 0)
		return;

    /* 根据虚拟运行时间判断当前正在执行的调度实例是否可以被抢占 */
	if (delta > ideal_runtime)
		resched_curr(rq_of(cfs_rq));
}

/*********************************************************************************************************
** 函数名称: set_next_entity
** 功能描述: 把指定的调度实例设置为指定的 cfs 运行队列的 current 并更新相关统调度计值
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end(cfs_rq, se);
		__dequeue_entity(cfs_rq, se);
	}

	update_stats_curr_start(cfs_rq, se);
	cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of(cfs_rq)->load.weight >= 2*se->load.weight) {
		se->statistics.slice_max = max(se->statistics.slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se);

/*
 * Pick the next process, keeping these things in mind, in this order:
 * 1) keep things fair between processes/task groups
 * 2) pick the "next" process, since someone really wants that to run
 * 3) pick the "last" process, for cache locality
 * 4) do not run the "skip" process, if something else is available
 */
/*********************************************************************************************************
** 函数名称: pick_next_entity
** 功能描述: 通过指定的函数参数计算指定的 cfs 运行队列下一次调度时需要运行的调度实例并返回
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : curr - 指定的当前 cpu 正在运行的调度实例指针
** 输	 出: se - 下一次调度需要运行的调度实例指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct sched_entity *
pick_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	struct sched_entity *left = __pick_first_entity(cfs_rq);
	struct sched_entity *se;

	/*
	 * If curr is set we have to see if its left of the leftmost entity
	 * still in the tree, provided there was anything in the tree at all.
	 */
	if (!left || (curr && entity_before(curr, left)))
		left = curr;

    /* 表示指定的 cfs 运行队列当前最需要执行（虚拟运行时间最小）的调度实例指针
       se = cfs_rq->rb_leftmost 或者 se = curr */
	se = left; /* ideally we run the leftmost entity */

	/*
	 * Avoid running the skip buddy, if running something else can
	 * be done without getting too unfair.
	 */
	/* 如果指定的 cfs 运行队列的 cfs_rq->skip 成员等于这个 cfs 运行队列中虚拟运行时间最小的调度实例 */
	if (cfs_rq->skip == se) {
		struct sched_entity *second;

		if (se == curr) {
            /* 表示我们需要跳过的调度实例 cfs_rq->skip 为 curr 并且 curr 调度实例的虚拟运行时间最小
               则设置 second 为 cfs_rq->rb_leftmost */
			second = __pick_first_entity(cfs_rq);
		} else {
		    /* 如果需要跳过的调度实例 cfs_rq->skip 虚拟运行时间最小且需要跳过的调度实例不是 curr
		       则设置 second 为虚拟时间和 cfs_rq->skip 最接近的下一个调度实例 */
			second = __pick_next_entity(se);

			/* 如果 cfs_rq->skip 最接近的下一个调度实例为空或者当前正在运行的调度实例虚拟运行时间时最小的
			   则设置 second 为 curr */
			if (!second || (curr && entity_before(curr, second)))
				second = curr;
		}

        /* 如果 left 调度实例不可以抢占 second 调度实例，则选择 second 调度实例 */
		if (second && wakeup_preempt_entity(second, left) < 1)
			se = second;
	}

    /* se - 存储了下一次调度时需要运行的调度实例指针 */
	
	/*
	 * Prefer last buddy, try to return the CPU to a preempted task.
	 */
	if (cfs_rq->last && wakeup_preempt_entity(cfs_rq->last, left) < 1)
		se = cfs_rq->last;

	/*
	 * Someone really wants this to run. If it's not unfair, run it.
	 */
	if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, left) < 1)
		se = cfs_rq->next;

	clear_buddies(cfs_rq, se);

	return se;
}

static bool check_cfs_rq_runtime(struct cfs_rq *cfs_rq);

/*********************************************************************************************************
** 函数名称: put_prev_entity
** 功能描述: 把指定的当前正在运行的调度实例按照虚拟运行时间添加到所属 cfs 运行队列的红黑树上并更新
**         : 相关调度统计值
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : prev - 指定的当前正在运行的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr(cfs_rq);

	/* throttle cfs_rqs exceeding runtime */
	check_cfs_rq_runtime(cfs_rq);

	check_spread(cfs_rq, prev);
	
	if (prev->on_rq) {
		update_stats_wait_start(cfs_rq, prev);
		/* Put 'current' back into the tree. */
		__enqueue_entity(cfs_rq, prev);
		/* in !on_rq case, update occurred at dequeue */
		update_entity_load_avg(prev, 1);
	}
	cfs_rq->curr = NULL;
}

/*********************************************************************************************************
** 函数名称: entity_tick
** 功能描述: 对当前正在运行的调度实例执行周期性操作，用来更新调度实例运行时统计信息，操作如下：
**         : 1. 更新当前运行队列的虚拟运行时间，物理运行时间，cfs 最小虚拟时间和带宽控制时间等时间信息
**         : 2. 更新当前的调度实例处于可运行状态时间对系统平均负载贡献值信息
**         : 3. 更新当前的 cfs 运行队列的阻塞负载贡献值以及综合平均负载贡献值信息
**         : 4. 更新当前的 cfs 运行队列所属任务组的 tg->shares 字段值
**         : 5. 检查指定的正在运行的调度实例是否已经运行了足够时间并被抢占，如果可以则在指定的 cpu 运行
**         :    队列上执行一次调度操作，判断是否可以抢占有两个条件，如下：
**         :    a. 如果当前正在执行的调度实例分配的物理时间片已经运行完，则表示可以被抢占 
**         :    b. 如果当前正在执行的调度实例运行的物理时间大于最小调度粒度且减去红黑树上虚拟运行时间最
**         :       小的调度实例的差值大于为当前调度实例分配的物理调度时间片，则表示可以被抢占
** 输	 入: cfs_rq- 指定的调度实例所属 cfs 运行队列指针
**         : curr - 指定的当前正在运行的调度实例指针
**         : queued - 当前 tick 是否为 queued ticks
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr, int queued)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	/*
	 * Ensure that runnable average is periodically updated.
	 */
	update_entity_load_avg(curr, 1);
	update_cfs_rq_blocked_load(cfs_rq, 1);
	update_cfs_shares(cfs_rq);

#ifdef CONFIG_SCHED_HRTICK
	/*
	 * queued ticks are scheduled to match the slice, so don't bother
	 * validating it and just reschedule.
	 */
	if (queued) {
		resched_curr(rq_of(cfs_rq));
		return;
	}
	/*
	 * don't let the period tick interfere with the hrtick preemption
	 */
	if (!sched_feat(DOUBLE_TICK) &&
			hrtimer_active(&rq_of(cfs_rq)->hrtick_timer))
		return;
#endif

	if (cfs_rq->nr_running > 1)
		check_preempt_tick(cfs_rq, curr);
}


/**************************************************
 * CFS bandwidth control machinery
 */

#ifdef CONFIG_CFS_BANDWIDTH

#ifdef HAVE_JUMP_LABEL
static struct static_key __cfs_bandwidth_used;

/*********************************************************************************************************
** 函数名称: cfs_bandwidth_used
** 功能描述: 返回当前 cfs 调度器是否使能带宽控制功能
** 输	 入: 
** 输	 出: true - 使能
**         : false - 不使能
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool cfs_bandwidth_used(void)
{
	return static_key_false(&__cfs_bandwidth_used);
}

void cfs_bandwidth_usage_inc(void)
{
	static_key_slow_inc(&__cfs_bandwidth_used);
}

void cfs_bandwidth_usage_dec(void)
{
	static_key_slow_dec(&__cfs_bandwidth_used);
}
#else /* HAVE_JUMP_LABEL */
/*********************************************************************************************************
** 函数名称: cfs_bandwidth_used
** 功能描述: 返回当前 cfs 调度器是否使能带宽控制功能
** 输	 入: 
** 输	 出: true - 使能
**         : false - 不使能
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool cfs_bandwidth_used(void)
{
	return true;
}

void cfs_bandwidth_usage_inc(void) {}
void cfs_bandwidth_usage_dec(void) {}
#endif /* HAVE_JUMP_LABEL */

/*
 * default period for cfs group bandwidth.
 * default: 0.1s, units: nanoseconds
 */
/*********************************************************************************************************
** 函数名称: default_cfs_period
** 功能描述: 获取当前 cfs 调度器使用的默认组带宽控制周期，0.1s
** 输	 入: 
** 输	 出: u64 - 组带宽控制周期时间，单位 ns
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 default_cfs_period(void)
{
	return 100000000ULL;
}

/*********************************************************************************************************
** 函数名称: sched_cfs_bandwidth_slice
** 功能描述: 获取当前 cfs 调度器使用的默认带宽控制 slice 值，即每次 cfs 运行队列可申请到的运行时间片
** 输	 入: 
** 输	 出: u64 - 带宽控制 slice 值，单位 ns
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 sched_cfs_bandwidth_slice(void)
{
	return (u64)sysctl_sched_cfs_bandwidth_slice * NSEC_PER_USEC;
}

/*
 * Replenish runtime according to assigned quota and update expiration time.
 * We use sched_clock_cpu directly instead of rq->clock to avoid adding
 * additional synchronization around rq->lock.
 *
 * requires cfs_b->lock
 */
/*********************************************************************************************************
** 函数名称: __refill_cfs_bandwidth_runtime
** 功能描述: 重新填充指定的带宽控制池到设定的默认状态
** 输	 入: cfs_b - 指定的带宽控制数据结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __refill_cfs_bandwidth_runtime(struct cfs_bandwidth *cfs_b)
{
	u64 now;

	if (cfs_b->quota == RUNTIME_INF)
		return;

	now = sched_clock_cpu(smp_processor_id());
	cfs_b->runtime = cfs_b->quota;
	cfs_b->runtime_expires = now + ktime_to_ns(cfs_b->period);
}

/*********************************************************************************************************
** 函数名称: tg_cfs_bandwidth
** 功能描述: 获取指定的任务组的带宽控制池指针
** 输	 入: tg - 指定的任务组结构指针
** 输	 出: &tg->cfs_bandwidth - 带宽控制池指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_bandwidth *tg_cfs_bandwidth(struct task_group *tg)
{
	return &tg->cfs_bandwidth;
}

/* rq->task_clock normalized against any time this cfs_rq has spent throttled */
/*********************************************************************************************************
** 函数名称: cfs_rq_clock_task
** 功能描述: 获取指定的 cfs 运行队列的任务时钟信息（在任务上下文的执行时间长度），单位是 ns
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: u64 - cfs 运行队列的任务时钟信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq)
{
	if (unlikely(cfs_rq->throttle_count))
		return cfs_rq->throttled_clock_task;

	return rq_clock_task(rq_of(cfs_rq)) - cfs_rq->throttled_clock_task_time;
}

/* returns 0 on failure to allocate runtime */
/*********************************************************************************************************
** 函数名称: assign_cfs_rq_runtime
** 功能描述: 尝试为指定的 cfs 运行队列从其所属任务组的带宽控制池中分配可运行时间片
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 1 - 分配成功
**         : 0 - 分配失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int assign_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct task_group *tg = cfs_rq->tg;
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(tg);
	u64 amount = 0, min_amount, expires;

	/* note: this is a positive sum as runtime_remaining <= 0 */
	min_amount = sched_cfs_bandwidth_slice() - cfs_rq->runtime_remaining;

	raw_spin_lock(&cfs_b->lock);
	if (cfs_b->quota == RUNTIME_INF)
		amount = min_amount;
	else {
		/*
		 * If the bandwidth pool has become inactive, then at least one
		 * period must have elapsed since the last consumption.
		 * Refresh the global state and ensure bandwidth timer becomes
		 * active.
		 */
		if (!cfs_b->timer_active) {
			__refill_cfs_bandwidth_runtime(cfs_b);
			__start_cfs_bandwidth(cfs_b, false);
		}

        /* 如果指定的 cfs 运行队列所属任务组的带宽控制池中还有剩余可运行时间，则分配给
		   指定的 cfs 运行队列 */
		if (cfs_b->runtime > 0) {
			amount = min(cfs_b->runtime, min_amount);
			cfs_b->runtime -= amount;
			cfs_b->idle = 0;
		}
	}
	expires = cfs_b->runtime_expires;
	raw_spin_unlock(&cfs_b->lock);

	cfs_rq->runtime_remaining += amount;
	/*
	 * we may have advanced our local expiration to account for allowed
	 * spread between our sched_clock and the one on which runtime was
	 * issued.
	 */
	if ((s64)(expires - cfs_rq->runtime_expires) > 0)
		cfs_rq->runtime_expires = expires;

	return cfs_rq->runtime_remaining > 0;
}

/*
 * Note: This depends on the synchronization provided by sched_clock and the
 * fact that rq->clock snapshots this value.
 */
/*********************************************************************************************************
** 函数名称: expire_cfs_rq_runtime
** 功能描述: 在本地 cfs 运行队列的带宽控制周期超时时调用，用来处理本地 cfs 运行队列的 runtime_expires 
**         : 和 runtime_remaining 字段变量值
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void expire_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);

	/* if the deadline is ahead of our clock, nothing to do */
	/* 如果当前 cfs 运行队列的带宽控制周期还没到期，则直接返回 */
	if (likely((s64)(rq_clock(rq_of(cfs_rq)) - cfs_rq->runtime_expires) < 0))
		return;

	if (cfs_rq->runtime_remaining < 0)
		return;

	/*
	 * If the local deadline has passed we have to consider the
	 * possibility that our sched_clock is 'fast' and the global deadline
	 * has not truly expired.
	 *
	 * Fortunately we can check determine whether this the case by checking
	 * whether the global deadline has advanced. It is valid to compare
	 * cfs_b->runtime_expires without any locks since we only care about
	 * exact equality, so a partial write will still work.
	 */
    /* 如果本地带宽控制超时期限已经达到，我们需要考虑是不是因为我们本地运行队列时钟
	   比全局运行队列时钟跑得快，而导致误判为本地运行队列统计周期已经超时

	   我们可以通过检查全局运行队列超时时间和本地运行队列超时时间是否相等决定是否发
	   生了这种情况，因为在整个地方没持有锁，所以部分写操作仍然是有效的，但是我们只
	   关心他们的 runtime_expires 是否相等，所以不持有锁也没有什么影响 */
	if (cfs_rq->runtime_expires != cfs_b->runtime_expires) {
		/* extend local deadline, drift is bounded above by 2 ticks */
		cfs_rq->runtime_expires += TICK_NSEC;
	} else {
		/* global deadline is ahead, expiration has passed */
		cfs_rq->runtime_remaining = 0;
	}
}

/*********************************************************************************************************
** 函数名称: __account_cfs_rq_runtime
** 功能描述: 用来处理指定的 cfs 运行队列消耗的指定的运行时间，主要在带宽控制时使用
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : delta_exec - 指定的物理运行时间
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec)
{
	/* dock delta_exec before expiring quota (as it could span periods) */
	cfs_rq->runtime_remaining -= delta_exec;
	expire_cfs_rq_runtime(cfs_rq);

    /* 判断当前 cfs 运行队列带宽控制时间片是否已经用尽，如果没用尽则直接返回 */
	if (likely(cfs_rq->runtime_remaining > 0))
		return;

	/*
	 * if we're unable to extend our runtime we resched so that the active
	 * hierarchy can be throttled
	 */
	/* 如果当前 cfs 运行队列可运行时间已用尽并且无法从带宽控制池中获取新的运行时间，则尝试
	   执行一次调度，唤醒其他可运行的任务 */
	if (!assign_cfs_rq_runtime(cfs_rq) && likely(cfs_rq->curr))
		resched_curr(rq_of(cfs_rq));
}

/*********************************************************************************************************
** 函数名称: account_cfs_rq_runtime
** 功能描述: 用来统计指定的 cfs 运行队列消耗的指定的物理运行时间，主要在带宽控制时使用
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : delta_exec - 指定的物理运行时间
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline
void account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec)
{
	if (!cfs_bandwidth_used() || !cfs_rq->runtime_enabled)
		return;

	__account_cfs_rq_runtime(cfs_rq, delta_exec);
}

/*********************************************************************************************************
** 函数名称: cfs_rq_throttled
** 功能描述: 判断指定的 cfs 运行队列是否处于 throttled 状态
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int cfs_rq_throttled(struct cfs_rq *cfs_rq)
{
	return cfs_bandwidth_used() && cfs_rq->throttled;
}

/*********************************************************************************************************
** 函数名称: cfs_rq_throttled
** 功能描述: 判断指定的 cfs 运行队列是否发生了 hierarchy throttled
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
/* check whether cfs_rq, or any parent, is throttled */
static inline int throttled_hierarchy(struct cfs_rq *cfs_rq)
{
	return cfs_bandwidth_used() && cfs_rq->throttle_count;
}

/*
 * Ensure that neither of the group entities corresponding to src_cpu or
 * dest_cpu are members of a throttled hierarchy when performing group
 * load-balance operations.
 */
/*********************************************************************************************************
** 函数名称: throttled_lb_pair
** 功能描述: 判断指定的任务组在指定的源 cpu 和目的 cpu 上的 cfs 运行队列是否处于 hierarchy throttled 状态
**         : 这个函数在任务组负载均衡时使用，用来判断是否可以进行任务组迁移
** 输	 入: tg - 指定的任务组结构指针
**         : src_cpu - 指定的源 cpu
**         : dest_cpu - 指定的目的 cpu
** 输	 出: 1 - 是 hierarchy throttled 状态
**         : 0 - 不是 hierarchy throttled 状态
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int throttled_lb_pair(struct task_group *tg,
				    int src_cpu, int dest_cpu)
{
	struct cfs_rq *src_cfs_rq, *dest_cfs_rq;

	src_cfs_rq = tg->cfs_rq[src_cpu];
	dest_cfs_rq = tg->cfs_rq[dest_cpu];

	return throttled_hierarchy(src_cfs_rq) ||
	       throttled_hierarchy(dest_cfs_rq);
}

/* updated child weight may affect parent so we have to do this bottom up */
/*********************************************************************************************************
** 函数名称: tg_unthrottle_up
** 功能描述: 在对指定的任务组树形结构遍历时执行的 up 操作，详情见 walk_tg_tree_from 函数
** 输	 入: tg - 指定的任务组指针
**         : data - 指定的 cpu 运行队列指针
** 输	 出: 0 - 执行成功
**         : other - 执行失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int tg_unthrottle_up(struct task_group *tg, void *data)
{
	struct rq *rq = data;
	struct cfs_rq *cfs_rq = tg->cfs_rq[cpu_of(rq)];

	cfs_rq->throttle_count--;
#ifdef CONFIG_SMP
	if (!cfs_rq->throttle_count) {
		/* adjust cfs_rq_clock_task() */
		cfs_rq->throttled_clock_task_time += rq_clock_task(rq) -
					     cfs_rq->throttled_clock_task;
	}
#endif

	return 0;
}

/*********************************************************************************************************
** 函数名称: tg_throttle_down
** 功能描述: 在对指定的任务组树形结构遍历时执行的 down 操作，详情见 walk_tg_tree_from 函数
** 输	 入: tg - 指定的任务组指针
**         : data - 指定的 cpu 运行队列指针
** 输	 出: 0 - 执行成功
**         : other - 执行失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int tg_throttle_down(struct task_group *tg, void *data)
{
	struct rq *rq = data;
	struct cfs_rq *cfs_rq = tg->cfs_rq[cpu_of(rq)];

	/* group is entering throttled state, stop time */
	if (!cfs_rq->throttle_count)
		cfs_rq->throttled_clock_task = rq_clock_task(rq);
	cfs_rq->throttle_count++;

	return 0;
}

/*********************************************************************************************************
** 函数名称: throttle_cfs_rq
** 功能描述: 对指定的 cfs 运行队列执行 throttle 操作
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void throttle_cfs_rq(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);
	struct sched_entity *se;
	long task_delta, dequeue = 1;

	se = cfs_rq->tg->se[cpu_of(rq_of(cfs_rq))];

	/* freeze hierarchy runnable averages while throttled */
	rcu_read_lock();
	walk_tg_tree_from(cfs_rq->tg, tg_throttle_down, tg_nop, (void *)rq);
	rcu_read_unlock();

	task_delta = cfs_rq->h_nr_running;

	/* 把执行 throttle 操作的 cfs 运行队列所属任务组从运行队列中移除并更新相关统计变量信息 */
	for_each_sched_entity(se) {
		struct cfs_rq *qcfs_rq = cfs_rq_of(se);
		/* throttled entity or throttle-on-deactivate */
		if (!se->on_rq)
			break;

		if (dequeue)
			dequeue_entity(qcfs_rq, se, DEQUEUE_SLEEP);
		qcfs_rq->h_nr_running -= task_delta;

		if (qcfs_rq->load.weight)
			dequeue = 0;
	}

    /* 如果遍历到任务组树的根节点位置，则需要同步更新所属 cpu 运行队列的调度实例统计变量值
	   因为任务组树根节点是挂接在 cpu 运行队列上的 */
	if (!se)
		sub_nr_running(rq, task_delta);

	cfs_rq->throttled = 1;
	cfs_rq->throttled_clock = rq_clock(rq);
	raw_spin_lock(&cfs_b->lock);
	/*
	 * Add to the _head_ of the list, so that an already-started
	 * distribute_cfs_runtime will not see us
	 */
	list_add_rcu(&cfs_rq->throttled_list, &cfs_b->throttled_cfs_rq);
	if (!cfs_b->timer_active)
		__start_cfs_bandwidth(cfs_b, false);
	raw_spin_unlock(&cfs_b->lock);
}

/*********************************************************************************************************
** 函数名称: unthrottle_cfs_rq
** 功能描述: 对指定的 cfs 运行队列执行 unthrottle 操作
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void unthrottle_cfs_rq(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);
	struct sched_entity *se;
	int enqueue = 1;
	long task_delta;

	se = cfs_rq->tg->se[cpu_of(rq)];

	cfs_rq->throttled = 0;

	update_rq_clock(rq);

	raw_spin_lock(&cfs_b->lock);
	cfs_b->throttled_time += rq_clock(rq) - cfs_rq->throttled_clock;
	list_del_rcu(&cfs_rq->throttled_list);
	raw_spin_unlock(&cfs_b->lock);

	/* update hierarchical throttle state */
	walk_tg_tree_from(cfs_rq->tg, tg_nop, tg_unthrottle_up, (void *)rq);

	if (!cfs_rq->load.weight)
		return;

	task_delta = cfs_rq->h_nr_running;
	for_each_sched_entity(se) {
		if (se->on_rq)
			enqueue = 0;

		cfs_rq = cfs_rq_of(se);
		if (enqueue)
			enqueue_entity(cfs_rq, se, ENQUEUE_WAKEUP);
		cfs_rq->h_nr_running += task_delta;

		if (cfs_rq_throttled(cfs_rq))
			break;
	}

	if (!se)
		add_nr_running(rq, task_delta);

	/* determine whether we need to wake up potentially idle cpu */
	/* 如果当前 cpu 上运行的是 idle 进程并且当前 cpu 运行队列包含其他的任务则尝试执行它们 */
	if (rq->curr == rq->idle && rq->cfs.nr_running)
		resched_curr(rq);
}

/*********************************************************************************************************
** 函数名称: distribute_cfs_runtime
** 功能描述: 把指定的可分配运行时间分配给指定的 cfs 带宽控制池中处于 throttled 状态的 cfs 运行队列
**         : 并对这些 cfs 运行队列执行 unthrottled 操作
** 输	 入: cfs_b - 指定的带宽控制池指针
**         : remaining - 指定的一共可分配运行时间
**         : expires - 指定的带宽控制统计周期超时时间
** 输	 出: u64 - 表示本次分配出去的可运行时间长度
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 distribute_cfs_runtime(struct cfs_bandwidth *cfs_b,
		u64 remaining, u64 expires)
{
	struct cfs_rq *cfs_rq;
	u64 runtime;
	u64 starting_runtime = remaining;

	rcu_read_lock();
	list_for_each_entry_rcu(cfs_rq, &cfs_b->throttled_cfs_rq,
				throttled_list) {
		struct rq *rq = rq_of(cfs_rq);

		raw_spin_lock(&rq->lock);
		if (!cfs_rq_throttled(cfs_rq))
			goto next;

        /* 表示当前 cfs 运行队列需要获取的可运行时间 */
		runtime = -cfs_rq->runtime_remaining + 1;
		
		if (runtime > remaining)
			runtime = remaining;
		remaining -= runtime;

		cfs_rq->runtime_remaining += runtime;
		cfs_rq->runtime_expires = expires;

		/* we check whether we're throttled above */
		if (cfs_rq->runtime_remaining > 0)
			unthrottle_cfs_rq(cfs_rq);

next:
		raw_spin_unlock(&rq->lock);

        /* 如果指定的可分配运行时间已经全部分配完毕，则直接退出返回 */
		if (!remaining)
			break;
	}
	rcu_read_unlock();

	return starting_runtime - remaining;
}

/*
 * Responsible for refilling a task_group's bandwidth and unthrottling its
 * cfs_rqs as appropriate. If there has been no activity within the last
 * period the timer is deactivated until scheduling resumes; cfs_b->idle is
 * used to track this state.
 */
/*********************************************************************************************************
** 函数名称: do_sched_cfs_period_timer
** 功能描述: cfs 带宽控制周期超时处理函数，用来为指定的带宽控制池分配指定份额的时间并尝试唤醒所有
**         : 处于 throttled 状态的 cfs 运行队列 
** 输	 入: cfs_b - 指定的带宽控制池指针
**         : overrun - 从上一次到现在流逝的统计周期个数
** 输	 出: 0 - cfs_b->timer 处于 active 状态
**         : 1 - cfs_b->timer 处于 deactive 状态
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int do_sched_cfs_period_timer(struct cfs_bandwidth *cfs_b, int overrun)
{
	u64 runtime, runtime_expires;
	int throttled;

	/* no need to continue the timer with no bandwidth constraint */
	if (cfs_b->quota == RUNTIME_INF)
		goto out_deactivate;

	throttled = !list_empty(&cfs_b->throttled_cfs_rq);
	cfs_b->nr_periods += overrun;

	/*
	 * idle depends on !throttled (for the case of a large deficit), and if
	 * we're going inactive then everything else can be deferred
	 */
	if (cfs_b->idle && !throttled)
		goto out_deactivate;

	/*
	 * if we have relooped after returning idle once, we need to update our
	 * status as actually running, so that other cpus doing
	 * __start_cfs_bandwidth will stop trying to cancel us.
	 */
	cfs_b->timer_active = 1;

    /* 重新为指定的 cfs 带宽控制池分配时间份额 */
	__refill_cfs_bandwidth_runtime(cfs_b);

	if (!throttled) {
		/* mark as potentially idle for the upcoming period */
		cfs_b->idle = 1;
		return 0;
	}

	/* account preceding periods in which throttling occurred */
	cfs_b->nr_throttled += overrun;

	runtime_expires = cfs_b->runtime_expires;

	/*
	 * This check is repeated as we are holding onto the new bandwidth while
	 * we unthrottle. This can potentially race with an unthrottled group
	 * trying to acquire new bandwidth from the global pool. This can result
	 * in us over-using our runtime if it is all used during this loop, but
	 * only by limited amounts in that extreme case.
	 */
	/* 尝试从当前 cfs 带宽控制池中分配时间给所有处于 throttled 状态的 cfs 运行队列 */
	while (throttled && cfs_b->runtime > 0) {
		runtime = cfs_b->runtime;
		raw_spin_unlock(&cfs_b->lock);
		/* we can't nest cfs_b->lock while distributing bandwidth */
		runtime = distribute_cfs_runtime(cfs_b, runtime,
						 runtime_expires);
		raw_spin_lock(&cfs_b->lock);

		throttled = !list_empty(&cfs_b->throttled_cfs_rq);

		cfs_b->runtime -= min(runtime, cfs_b->runtime);
	}

	/*
	 * While we are ensured activity in the period following an
	 * unthrottle, this also covers the case in which the new bandwidth is
	 * insufficient to cover the existing bandwidth deficit.  (Forcing the
	 * timer to remain active while there are any throttled entities.)
	 */
	cfs_b->idle = 0;

	return 0;

out_deactivate:
	cfs_b->timer_active = 0;
	return 1;
}

/* a cfs_rq won't donate quota below this amount */
static const u64 min_cfs_rq_runtime = 1 * NSEC_PER_MSEC;
/* minimum remaining period time to redistribute slack quota */
static const u64 min_bandwidth_expiration = 2 * NSEC_PER_MSEC;
/* how long we wait to gather additional slack before distributing */
static const u64 cfs_bandwidth_slack_period = 5 * NSEC_PER_MSEC;

/*
 * Are we near the end of the current quota period?
 *
 * Requires cfs_b->lock for hrtimer_expires_remaining to be safe against the
 * hrtimer base being cleared by __hrtimer_start_range_ns. In the case of
 * migrate_hrtimers, base is never cleared, so we are fine.
 */
/*********************************************************************************************************
** 函数名称: runtime_refresh_within
** 功能描述: 判断指定的 cfs 带宽控制池当前带宽控制统计周期是否即将结束，进入下一个统计周期
** 输	 入: cfs_b - 指定的带宽控制池指针
**         : min_expire - 指定的剩余时间判断阈值，如果剩余时间小于这个值，即判为即将结束
** 输	 出: 1 - 即将结束
**         : 0 - 没即将结束
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int runtime_refresh_within(struct cfs_bandwidth *cfs_b, u64 min_expire)
{
	struct hrtimer *refresh_timer = &cfs_b->period_timer;
	u64 remaining;

	/* if the call-back is running a quota refresh is already occurring */
	if (hrtimer_callback_running(refresh_timer))
		return 1;

	/* is a quota refresh about to occur? */
	remaining = ktime_to_ns(hrtimer_expires_remaining(refresh_timer));
	if (remaining < min_expire)
		return 1;

	return 0;
}

/*********************************************************************************************************
** 函数名称: start_cfs_slack_bandwidth
** 功能描述: 启动指定的带宽控制池的 slack 定时器
** 输	 入: cfs_b - 指定的带宽控制池指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void start_cfs_slack_bandwidth(struct cfs_bandwidth *cfs_b)
{
	u64 min_left = cfs_bandwidth_slack_period + min_bandwidth_expiration;

	/* if there's a quota refresh soon don't bother with slack */
	if (runtime_refresh_within(cfs_b, min_left))
		return;

	start_bandwidth_timer(&cfs_b->slack_timer,
				ns_to_ktime(cfs_bandwidth_slack_period));
}

/* we know any runtime found here is valid as update_curr() precedes return */
/*********************************************************************************************************
** 函数名称: __return_cfs_rq_runtime
** 功能描述: 尝试从指定的 cfs 运行队列中拿出多余的可运行时间放到所属任务组的带宽控制池中并启动指定的
**         : 带宽控制池的 slack 定时器
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __return_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	struct cfs_bandwidth *cfs_b = tg_cfs_bandwidth(cfs_rq->tg);
	s64 slack_runtime = cfs_rq->runtime_remaining - min_cfs_rq_runtime;

	if (slack_runtime <= 0)
		return;

	raw_spin_lock(&cfs_b->lock);
	if (cfs_b->quota != RUNTIME_INF &&
	    cfs_rq->runtime_expires == cfs_b->runtime_expires) {
		cfs_b->runtime += slack_runtime;

		/* we are under rq->lock, defer unthrottling using a timer */
		if (cfs_b->runtime > sched_cfs_bandwidth_slice() &&
		    !list_empty(&cfs_b->throttled_cfs_rq))
			start_cfs_slack_bandwidth(cfs_b);
	}
	raw_spin_unlock(&cfs_b->lock);

	/* even if it's not valid for return we don't want to try again */
	cfs_rq->runtime_remaining -= slack_runtime;
}

/*********************************************************************************************************
** 函数名称: return_cfs_rq_runtime
** 功能描述: 尝试从指定的 cfs 运行队列中拿出多余的可运行时间放到所属任务组的带宽控制池中并启动指定的
**         : 带宽控制池的 slack 定时器
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void return_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return;

	if (!cfs_rq->runtime_enabled || cfs_rq->nr_running)
		return;

	__return_cfs_rq_runtime(cfs_rq);
}

/*
 * This is done with a timer (instead of inline with bandwidth return) since
 * it's necessary to juggle rq->locks to unthrottle their respective cfs_rqs.
 */
/*********************************************************************************************************
** 函数名称: do_sched_cfs_slack_timer
** 功能描述: 尝试把指定的带宽控制池中剩余可运行时间分配给处于 throttled 状态的 cfs 运行队列
**         : 并对这些 cfs 运行队列执行 unthrottled 操作
** 输	 入: cfs_b - 指定的带宽控制池指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void do_sched_cfs_slack_timer(struct cfs_bandwidth *cfs_b)
{
	u64 runtime = 0, slice = sched_cfs_bandwidth_slice();
	u64 expires;

	/* confirm we're still not at a refresh boundary */
	raw_spin_lock(&cfs_b->lock);
	if (runtime_refresh_within(cfs_b, min_bandwidth_expiration)) {
		raw_spin_unlock(&cfs_b->lock);
		return;
	}

	if (cfs_b->quota != RUNTIME_INF && cfs_b->runtime > slice)
		runtime = cfs_b->runtime;

	expires = cfs_b->runtime_expires;
	raw_spin_unlock(&cfs_b->lock);

	if (!runtime)
		return;

	runtime = distribute_cfs_runtime(cfs_b, runtime, expires);

	raw_spin_lock(&cfs_b->lock);
	if (expires == cfs_b->runtime_expires)
		cfs_b->runtime -= min(runtime, cfs_b->runtime);
	raw_spin_unlock(&cfs_b->lock);
}

/*
 * When a group wakes up we want to make sure that its quota is not already
 * expired/exceeded, otherwise it may be allowed to steal additional ticks of
 * runtime as update_curr() throttling can not not trigger until it's on-rq.
 */
/*********************************************************************************************************
** 函数名称: check_enqueue_throttle
** 功能描述: 在向指定的 cfs 运行队列中添加新的调度实例时用来判断指定的 cfs 运行队列是否需要进入
**         : throttle 状态并执行相应的操作
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void check_enqueue_throttle(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return;

	/* an active group must be handled by the update_curr()->put() path */
	if (!cfs_rq->runtime_enabled || cfs_rq->curr)
		return;

	/* ensure the group is not already throttled */
	if (cfs_rq_throttled(cfs_rq))
		return;

	/* update runtime allocation */
	account_cfs_rq_runtime(cfs_rq, 0);

	/* 如果指定的 cfs 运行队列已经没有可运行时间了，则对其执行 throttle 操作 */
	if (cfs_rq->runtime_remaining <= 0)
		throttle_cfs_rq(cfs_rq);
}

/* conditionally throttle active cfs_rq's from put_prev_entity() */
/*********************************************************************************************************
** 函数名称: check_cfs_rq_runtime
** 功能描述: 尝试根据指定的 cfs 运行队列的 runtime_remaining 对其执行 throttle 操作
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: true - 执行了 throttle 操作
**         : false - 没执行 throttle 操作
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool check_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	if (!cfs_bandwidth_used())
		return false;

	if (likely(!cfs_rq->runtime_enabled || cfs_rq->runtime_remaining > 0))
		return false;

	/*
	 * it's possible for a throttled entity to be forced into a running
	 * state (e.g. set_curr_task), in this case we're finished.
	 */
	if (cfs_rq_throttled(cfs_rq))
		return true;

	throttle_cfs_rq(cfs_rq);
	return true;
}

/*********************************************************************************************************
** 函数名称: do_sched_cfs_slack_timer
** 功能描述: 尝试把指定的带宽控制池中剩余可运行时间分配给处于 throttled 状态的 cfs 运行队列
**         : 并对这些 cfs 运行队列执行 unthrottled 操作
** 输	 入: timer - 指定的带宽控制池的 slack 高精定时器指针
** 输	 出: HRTIMER_NORESTART - 不需要重新启动高精定时器
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static enum hrtimer_restart sched_cfs_slack_timer(struct hrtimer *timer)
{
	struct cfs_bandwidth *cfs_b =
		container_of(timer, struct cfs_bandwidth, slack_timer);
	do_sched_cfs_slack_timer(cfs_b);

	return HRTIMER_NORESTART;
}

/*********************************************************************************************************
** 函数名称: sched_cfs_period_timer
** 功能描述: cfs 带宽控制周期超时处理函数，用来为指定的带宽控制池分配指定份额的时间并尝试唤醒所有
**         : 处于 throttled 状态的 cfs 运行队列 
** 输	 入: timer - 指定的带宽控制池的 period 高精定时器指针
** 输	 出: HRTIMER_NORESTART - 不需要重新启动高精定时器
**         : HRTIMER_RESTART - 需要重新启动高精定时器
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static enum hrtimer_restart sched_cfs_period_timer(struct hrtimer *timer)
{
	struct cfs_bandwidth *cfs_b =
		container_of(timer, struct cfs_bandwidth, period_timer);
	ktime_t now;
	int overrun;
	int idle = 0;

	raw_spin_lock(&cfs_b->lock);
	for (;;) {
		now = hrtimer_cb_get_time(timer);
		overrun = hrtimer_forward(timer, now, cfs_b->period);

		if (!overrun)
			break;

		idle = do_sched_cfs_period_timer(cfs_b, overrun);
	}
	raw_spin_unlock(&cfs_b->lock);

	return idle ? HRTIMER_NORESTART : HRTIMER_RESTART;
}

/*********************************************************************************************************
** 函数名称: init_cfs_bandwidth
** 功能描述: 初始化指定的 cfs 带宽控制结构
** 输	 入: cfs_b - 指定的 cfs 带宽控制结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void init_cfs_bandwidth(struct cfs_bandwidth *cfs_b)
{
	raw_spin_lock_init(&cfs_b->lock);
	cfs_b->runtime = 0;
	cfs_b->quota = RUNTIME_INF;
	cfs_b->period = ns_to_ktime(default_cfs_period());

	INIT_LIST_HEAD(&cfs_b->throttled_cfs_rq);
	hrtimer_init(&cfs_b->period_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cfs_b->period_timer.function = sched_cfs_period_timer;
	hrtimer_init(&cfs_b->slack_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cfs_b->slack_timer.function = sched_cfs_slack_timer;
}

/*********************************************************************************************************
** 函数名称: init_cfs_rq_runtime
** 功能描述: 初始化指定的 cfs 运行队列中和带宽控制运行时时间相关的成员
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void init_cfs_rq_runtime(struct cfs_rq *cfs_rq)
{
	cfs_rq->runtime_enabled = 0;
	INIT_LIST_HEAD(&cfs_rq->throttled_list);
}

/* requires cfs_b->lock, may release to reprogram timer */
/*********************************************************************************************************
** 函数名称: __start_cfs_bandwidth
** 功能描述: 根据函数指定的参数启动指定的 cfs 带宽控制的高精度定时器
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : force - 是否直接强制启动
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __start_cfs_bandwidth(struct cfs_bandwidth *cfs_b, bool force)
{
	/*
	 * The timer may be active because we're trying to set a new bandwidth
	 * period or because we're racing with the tear-down path
	 * (timer_active==0 becomes visible before the hrtimer call-back
	 * terminates).  In either case we ensure that it's re-programmed
	 */
	/* 如果指定的 cfs 带宽控制的高精度定时器正在执行超时处理回调函数并且当前
	   没指定强制启动定时器，则等待高精度定时器超时处理回调函数执行完成 */
	while (unlikely(hrtimer_active(&cfs_b->period_timer)) &&
	       hrtimer_try_to_cancel(&cfs_b->period_timer) < 0) {
		/* bounce the lock to allow do_sched_cfs_period_timer to run */
		raw_spin_unlock(&cfs_b->lock);
		cpu_relax();
		raw_spin_lock(&cfs_b->lock);
		/* if someone else restarted the timer then we're done */
		if (!force && cfs_b->timer_active)
			return;
	}

	cfs_b->timer_active = 1;
	start_bandwidth_timer(&cfs_b->period_timer, cfs_b->period);
}

/*********************************************************************************************************
** 函数名称: destroy_cfs_bandwidth
** 功能描述: 尝试销毁指定的 cfs 带宽控制结构
** 输	 入: cfs_b- 指定的 cfs 带宽控制结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void destroy_cfs_bandwidth(struct cfs_bandwidth *cfs_b)
{
	/* init_cfs_bandwidth() was not called */
	if (!cfs_b->throttled_cfs_rq.next)
		return;

	hrtimer_cancel(&cfs_b->period_timer);
	hrtimer_cancel(&cfs_b->slack_timer);
}

/*********************************************************************************************************
** 函数名称: update_runtime_enabled
** 功能描述: 根据指定的 cpu 运行队列上的任务组的带宽控制状态更新对应的 cfs_rq->runtime_enabled 成员值
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __maybe_unused update_runtime_enabled(struct rq *rq)
{
	struct cfs_rq *cfs_rq;

	for_each_leaf_cfs_rq(rq, cfs_rq) {
		struct cfs_bandwidth *cfs_b = &cfs_rq->tg->cfs_bandwidth;

		raw_spin_lock(&cfs_b->lock);
		cfs_rq->runtime_enabled = cfs_b->quota != RUNTIME_INF;
		raw_spin_unlock(&cfs_b->lock);
	}
}

/*********************************************************************************************************
** 函数名称: unthrottle_offline_cfs_rqs
** 功能描述: 对转换到 offline 状态的 cpu 运行队列中处于 throttled 状态的 cfs 运行队列执行 unthrottle 操作
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __maybe_unused unthrottle_offline_cfs_rqs(struct rq *rq)
{
	struct cfs_rq *cfs_rq;

	for_each_leaf_cfs_rq(rq, cfs_rq) {
		if (!cfs_rq->runtime_enabled)
			continue;

		/*
		 * clock_task is not advancing so we just need to make sure
		 * there's some valid quota amount
		 */
		cfs_rq->runtime_remaining = 1;
		/*
		 * Offline rq is schedulable till cpu is completely disabled
		 * in take_cpu_down(), so we prevent new cfs throttling here.
		 */
		cfs_rq->runtime_enabled = 0;

		if (cfs_rq_throttled(cfs_rq))
			unthrottle_cfs_rq(cfs_rq);
	}
}

#else /* CONFIG_CFS_BANDWIDTH */
/*********************************************************************************************************
** 函数名称: cfs_rq_clock_task
** 功能描述: 获取指定的 cfs 运行队列的任务时钟信息（在任务上下文的执行时间长度），单位是 ns
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: u64 - cfs 运行队列的任务时钟信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 cfs_rq_clock_task(struct cfs_rq *cfs_rq)
{
	return rq_clock_task(rq_of(cfs_rq));
}

/*********************************************************************************************************
** 函数名称: account_cfs_rq_runtime
** 功能描述: 用来统计指定的 cfs 运行队列消耗的指定的物理运行时间，主要在带宽控制时使用
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
**         : delta_exec - 指定的物理运行时间
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec) {}

/*********************************************************************************************************
** 函数名称: check_cfs_rq_runtime
** 功能描述: 尝试根据指定的 cfs 运行队列的 runtime_remaining 对其执行 throttle 操作
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: true - 执行了 throttle 操作
**         : false - 没执行 throttle 操作
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool check_cfs_rq_runtime(struct cfs_rq *cfs_rq) { return false; }

/*********************************************************************************************************
** 函数名称: check_enqueue_throttle
** 功能描述: 在向指定的 cfs 运行队列中添加新的调度实例时用来判断指定的 cfs 运行队列是否需要进入
**         : throttle 状态并执行相应的操作
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void check_enqueue_throttle(struct cfs_rq *cfs_rq) {}

/*********************************************************************************************************
** 函数名称: return_cfs_rq_runtime
** 功能描述: 尝试从指定的 cfs 运行队列中拿出多余的可运行时间放到所属任务组的带宽控制池中并启动指定的
**         : 带宽控制池的 slack 定时器
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void return_cfs_rq_runtime(struct cfs_rq *cfs_rq) {}

/*********************************************************************************************************
** 函数名称: cfs_rq_throttled
** 功能描述: 判断指定的 cfs 运行队列是否已经 throttled 了
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int cfs_rq_throttled(struct cfs_rq *cfs_rq)
{
	return 0;
}

/*********************************************************************************************************
** 函数名称: cfs_rq_throttled
** 功能描述: 判断指定的 cfs 运行队列是否发生了 hierarchy throttled
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int throttled_hierarchy(struct cfs_rq *cfs_rq)
{
	return 0;
}

/*********************************************************************************************************
** 函数名称: throttled_lb_pair
** 功能描述: 判断指定的任务组在指定的源 cpu 和目的 cpu 上的 cfs 运行队列是否处于 hierarchy throttled 状态
**         : 这个函数在任务组负载均衡时使用，用来判断是否可以进行任务组迁移
** 输	 入: tg - 指定的任务组结构指针
**         : src_cpu - 指定的源 cpu
**         : dest_cpu - 指定的目的 cpu
** 输	 出: 1 - 是 hierarchy throttled 状态
**         : 0 - 不是 hierarchy throttled 状态
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int throttled_lb_pair(struct task_group *tg,
				    int src_cpu, int dest_cpu)
{
	return 0;
}

/*********************************************************************************************************
** 函数名称: init_cfs_bandwidth
** 功能描述: 初始化指定的 cfs 带宽控制结构
** 输	 入: cfs_b - 指定的 cfs 带宽控制结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void init_cfs_bandwidth(struct cfs_bandwidth *cfs_b) {}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*********************************************************************************************************
** 函数名称: init_cfs_rq_runtime
** 功能描述: 初始化指定的 cfs 运行队列中和带宽控制运行时时间相关的成员
** 输	 入: cfs_rq- 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void init_cfs_rq_runtime(struct cfs_rq *cfs_rq) {}
#endif

/*********************************************************************************************************
** 函数名称: tg_cfs_bandwidth
** 功能描述: 获取指定的任务组的带宽控制池指针
** 输	 入: tg - 指定的任务组结构指针
** 输	 出: &tg->cfs_bandwidth - 带宽控制池指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cfs_bandwidth *tg_cfs_bandwidth(struct task_group *tg)
{
	return NULL;
}

/*********************************************************************************************************
** 函数名称: destroy_cfs_bandwidth
** 功能描述: 尝试销毁指定的 cfs 带宽控制结构
** 输	 入: cfs_b- 指定的 cfs 带宽控制结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void destroy_cfs_bandwidth(struct cfs_bandwidth *cfs_b) {}

/*********************************************************************************************************
** 函数名称: update_runtime_enabled
** 功能描述: 根据指定的 cpu 运行队列上的任务组的带宽控制状态更新对应的 cfs_rq->runtime_enabled 成员值
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_runtime_enabled(struct rq *rq) {}

/*********************************************************************************************************
** 函数名称: unthrottle_offline_cfs_rqs
** 功能描述: 对转换到 offline 状态的 cpu 运行队列中处于 throttled 状态的 cfs 运行队列执行 unthrottle 操作
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void unthrottle_offline_cfs_rqs(struct rq *rq) {}

#endif /* CONFIG_CFS_BANDWIDTH */

/**************************************************
 * CFS operations on tasks:
 */

#ifdef CONFIG_SCHED_HRTICK
/*********************************************************************************************************
** 函数名称: hrtick_start_fair
** 功能描述: 在指定的 cpu 运行队列上为指定的任务启动高精度定时器，这个定时器会在指定的任务分配的时间
**         : 份额用尽时到期并执行超时处理函数
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	WARN_ON(task_rq(p) != rq);

	if (cfs_rq->nr_running > 1) {
		u64 slice = sched_slice(cfs_rq, se);
		u64 ran = se->sum_exec_runtime - se->prev_sum_exec_runtime;
		s64 delta = slice - ran;

		if (delta < 0) {
			if (rq->curr == p)
				resched_curr(rq);
			return;
		}
		hrtick_start(rq, delta);
	}
}

/*
 * called from enqueue/dequeue and updates the hrtick when the
 * current task is from our class and nr_running is low enough
 * to matter.
 */
/*********************************************************************************************************
** 函数名称: hrtick_update
** 功能描述: 尝试为指定的 cpu 运行队列当前正在运行的任务启动高精度定时器，这个定时器会在当前运行的任务
**         : 分配的时间份额用尽时到期并执行超时处理函数
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void hrtick_update(struct rq *rq)
{
	struct task_struct *curr = rq->curr;

	if (!hrtick_enabled(rq) || curr->sched_class != &fair_sched_class)
		return;

	if (cfs_rq_of(&curr->se)->nr_running < sched_nr_latency)
		hrtick_start_fair(rq, curr);
}
#else /* !CONFIG_SCHED_HRTICK */

/*********************************************************************************************************
** 函数名称: hrtick_start_fair
** 功能描述: 在指定的 cpu 运行队列上为指定的任务启动高精度定时器，这个定时器会在指定的任务分配的时间
**         : 份额用尽时到期并执行超时处理函数
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
}

/*********************************************************************************************************
** 函数名称: hrtick_update
** 功能描述: 尝试为指定的 cpu 运行队列当前正在运行的任务启动高精度定时器，这个定时器会在当前运行的任务
**         : 分配的时间份额用尽时到期并执行超时处理函数
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void hrtick_update(struct rq *rq)
{
}
#endif

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
/*********************************************************************************************************
** 函数名称: enqueue_task_fair
** 功能描述: 向指定的 cpu 运行队列的 cfs 运行队列中添加指定的新的任务并更新相关统计变量值
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
**         : flags - 指定的 ENQUEUE flags，例如 ENQUEUE_WAKEUP
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
enqueue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, flags);

		/*
		 * end evaluation on encountering a throttled cfs_rq
		 *
		 * note: in the case of encountering a throttled cfs_rq we will
		 * post the final h_nr_running increment below.
		*/
		if (cfs_rq_throttled(cfs_rq))
			break;
		cfs_rq->h_nr_running++;

		flags = ENQUEUE_WAKEUP;
	}

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		cfs_rq->h_nr_running++;

		if (cfs_rq_throttled(cfs_rq))
			break;

		update_cfs_shares(cfs_rq);
		update_entity_load_avg(se, 1);
	}

	if (!se) {
		update_rq_runnable_avg(rq, rq->nr_running);
		add_nr_running(rq, 1);
	}
	hrtick_update(rq);
}

static void set_next_buddy(struct sched_entity *se);

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
/*********************************************************************************************************
** 函数名称: enqueue_task_fair
** 功能描述: 从指定的 cpu 运行队列的 cfs 运行队列中移除指定的任务并更新相关统计变量值
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
**         : flags - 指定的 DEQUEUE flags，例如 DEQUEUE_SLEEP
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int flags)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;
	int task_sleep = flags & DEQUEUE_SLEEP;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		dequeue_entity(cfs_rq, se, flags);

		/*
		 * end evaluation on encountering a throttled cfs_rq
		 *
		 * note: in the case of encountering a throttled cfs_rq we will
		 * post the final h_nr_running decrement below.
		*/
		if (cfs_rq_throttled(cfs_rq))
			break;
		cfs_rq->h_nr_running--;

		/* Don't dequeue parent if it has other entities besides us */
		if (cfs_rq->load.weight) {
			/*
			 * Bias pick_next to pick a task from this cfs_rq, as
			 * p is sleeping when it is within its sched_slice.
			 */
			if (task_sleep && parent_entity(se))
				set_next_buddy(parent_entity(se));

			/* avoid re-evaluating load for this entity */
			se = parent_entity(se);
			break;
		}
		flags |= DEQUEUE_SLEEP;
	}

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		cfs_rq->h_nr_running--;

		if (cfs_rq_throttled(cfs_rq))
			break;

		update_cfs_shares(cfs_rq);
		update_entity_load_avg(se, 1);
	}

	if (!se) {
		sub_nr_running(rq, 1);
		update_rq_runnable_avg(rq, 1);
	}
	hrtick_update(rq);
}

#ifdef CONFIG_SMP
/* Used instead of source_load when we know the type == 0 */
/*********************************************************************************************************
** 函数名称: weighted_cpuload
** 功能描述: 获取指定的 cpu 的 cfs 运行队列在过去一段时间内经过衰减的“加权”平均负载贡献统计信息
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: long - “加权”平均负载贡献统计信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long weighted_cpuload(const int cpu)
{
	return cpu_rq(cpu)->cfs.runnable_load_avg;
}

/*
 * Return a low guess at the load of a migration-source cpu weighted
 * according to the scheduling class and "nice" value.
 *
 * We want to under-estimate the load of migration sources, to
 * balance conservatively.
 */
/*********************************************************************************************************
** 函数名称: source_load
** 功能描述: 获取指定的 cpu 运行队列源负载贡献统计信息值
** 输	 入: cpu - 指定的 cpu id 值
**         : type - 指定的 cpu 负载类型
** 输	 出: unsigned long - 负载贡献统计信息的最大值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long source_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return min(rq->cpu_load[type-1], total);
}

/*
 * Return a high guess at the load of a migration-target cpu weighted
 * according to the scheduling class and "nice" value.
 */
/*********************************************************************************************************
** 函数名称: source_load
** 功能描述: 获取指定的 cpu 运行队列目的负载贡献统计信息值
** 输	 入: cpu - 指定的 cpu id 值
**         : type - 指定的 cpu 负载类型
** 输	 出: unsigned long - 负载贡献统计信息的最大值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long target_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return max(rq->cpu_load[type-1], total);
}

/*********************************************************************************************************
** 函数名称: capacity_of
** 功能描述: 获取指定的 cpu 的计算能力数据信息
** 注     释: 这个计算能力是以 SCHED_CAPACITY_SCALE 为基准归一化后的计算能力
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: long - 计算能力数据信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long capacity_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

/*********************************************************************************************************
** 函数名称: cpu_avg_load_per_task
** 功能描述: 获取指定的 cpu 上 cfs 运行队列的运行负载平均到每个任务上是多少
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: unsigned long - 平均到每个任务的负载值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long cpu_avg_load_per_task(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long nr_running = ACCESS_ONCE(rq->cfs.h_nr_running);
	unsigned long load_avg = rq->cfs.runnable_load_avg;

	if (nr_running)
		return load_avg / nr_running;

	return 0;
}

/*********************************************************************************************************
** 函数名称: record_wakee
** 功能描述: 在当前任务执行唤醒其他任务操作时记录相关数据
** 输	 入: p - 指定的被唤醒任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void record_wakee(struct task_struct *p)
{
	/*
	 * Rough decay (wiping) for cost saving, don't worry
	 * about the boundary, really active task won't care
	 * about the loss.
	 */
	if (time_after(jiffies, current->wakee_flip_decay_ts + HZ)) {
		current->wakee_flips >>= 1;
		current->wakee_flip_decay_ts = jiffies;
	}

	if (current->last_wakee != p) {
		current->last_wakee = p;
		current->wakee_flips++;
	}
}

/*********************************************************************************************************
** 函数名称: task_waking_fair
** 功能描述: 用来唤醒指定的任务并记录相关数据
** 输	 入: p - 指定的被唤醒任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_waking_fair(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	u64 min_vruntime;

#ifndef CONFIG_64BIT
	u64 min_vruntime_copy;

	do {
		min_vruntime_copy = cfs_rq->min_vruntime_copy;
		smp_rmb();
		min_vruntime = cfs_rq->min_vruntime;
	} while (min_vruntime != min_vruntime_copy);
#else
	min_vruntime = cfs_rq->min_vruntime;
#endif

	se->vruntime -= min_vruntime;
	record_wakee(p);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*
 * effective_load() calculates the load change as seen from the root_task_group
 *
 * Adding load to a group doesn't make a group heavier, but can cause movement
 * of group shares between cpus. Assuming the shares were perfectly aligned one
 * can calculate the shift in shares.
 *
 * Calculate the effective load difference if @wl is added (subtracted) to @tg
 * on this @cpu and results in a total addition (subtraction) of @wg to the
 * total group weight.
 *
 * Given a runqueue weight distribution (rw_i) we can compute a shares
 * distribution (s_i) using:
 *
 *   s_i = rw_i / \Sum rw_j						(1)
 *
 * Suppose we have 4 CPUs and our @tg is a direct child of the root group and
 * has 7 equal weight tasks, distributed as below (rw_i), with the resulting
 * shares distribution (s_i):
 *
 *   rw_i = {   2,   4,   1,   0 }
 *   s_i  = { 2/7, 4/7, 1/7,   0 }
 *
 * As per wake_affine() we're interested in the load of two CPUs (the CPU the
 * task used to run on and the CPU the waker is running on), we need to
 * compute the effect of waking a task on either CPU and, in case of a sync
 * wakeup, compute the effect of the current task going to sleep.
 *
 * So for a change of @wl to the local @cpu with an overall group weight change
 * of @wl we can compute the new shares distribution (s'_i) using:
 *
 *   s'_i = (rw_i + @wl) / (@wg + \Sum rw_j)				(2)
 *
 * Suppose we're interested in CPUs 0 and 1, and want to compute the load
 * differences in waking a task to CPU 0. The additional task changes the
 * weight and shares distributions like:
 *
 *   rw'_i = {   3,   4,   1,   0 }
 *   s'_i  = { 3/8, 4/8, 1/8,   0 }
 *
 * We can then compute the difference in effective weight by using:
 *
 *   dw_i = S * (s'_i - s_i)						(3)
 *
 * Where 'S' is the group weight as seen by its parent.
 *
 * Therefore the effective change in loads on CPU 0 would be 5/56 (3/8 - 2/7)
 * times the weight of the group. The effect on CPU 1 would be -4/56 (4/8 -
 * 4/7) times the weight of the group.
 */
/*********************************************************************************************************
** 函数名称: effective_load
** 功能描述: 在向指定的任务组中添加新的任务时用来计算所属任务组树根节点的权重信息变化量
** 输	 入: tg - 指定的任务组指针
**         : cpu - 指定的 cpu id
**         : wl - 指定的任务组的 cfs 运行队列权重信息变化量
**         : wg - 指定的任务组权重信息变化量
** 输	 出: wl - 任务组树根节点的权重信息变化量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static long effective_load(struct task_group *tg, int cpu, long wl, long wg)
{
	struct sched_entity *se = tg->se[cpu];

	if (!tg->parent)	/* the trivial, non-cgroup case */
		return wl;

	for_each_sched_entity(se) {
		long w, W;

		tg = se->my_q->tg;

		/* W = @wg + \Sum rw_j，表示指定的任务组总的权重信息 */
		W = wg + calc_tg_weight(tg, se->my_q);

		/* w = rw_i + @wl，表示指定的任务组上的 cfs 运行队列总的权重信息 */
		w = se->my_q->load.weight + wl;

		/* wl = S * s'_i; see (2) 
		   wl = S * ((rw_i + @wl) / (@wg + \Sum rw_j)) */
		if (W > 0 && w < W)
			wl = (w * (long)tg->shares) / W;
		else
			wl = tg->shares;

		/*
		 * Per the above, wl is the new se->load.weight value; since
		 * those are clipped to [MIN_SHARES, ...) do so now. See
		 * calc_cfs_shares().
		 */
		if (wl < MIN_SHARES)
			wl = MIN_SHARES;

		/* wl = dw_i = S * (s'_i - s_i); see (3)
		   wl = S * ((rw_i + @wl) / (@wg + \Sum rw_j)) - S * rw_i / \Sum rw_j */
		wl -= se->load.weight;

		/*
		 * Recursively apply this logic to all parent groups to compute
		 * the final effective load change on the root group. Since
		 * only the @tg group gets extra weight, all parent groups can
		 * only redistribute existing shares. @wl is the shift in shares
		 * resulting from this level per the above.
		 */
		wg = 0;
	}

	return wl;
}
#else

/*********************************************************************************************************
** 函数名称: effective_load
** 功能描述: 在向指定的任务组中添加新的任务时用来计算所属任务组树根节点的权重信息变化量
** 输	 入: tg - 指定的任务组指针
**         : cpu - 指定的 cpu id
**         : wl - 指定的任务组的 cfs 运行队列权重信息变化量
**         : wg - 指定的任务组权重信息变化量
** 输	 出: wl - 任务组树根节点的权重信息变化量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static long effective_load(struct task_group *tg, int cpu, long wl, long wg)
{
	return wl;
}

#endif

/*********************************************************************************************************
** 函数名称: wake_wide
** 功能描述: 判断当前正在执行的任务是否在短时间内唤醒了多个其他任务
** 输	 入: p - 指定的被唤醒任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int wake_wide(struct task_struct *p)
{
    /* 详情见 update_top_cache_domain 函数 */
	int factor = this_cpu_read(sd_llc_size);

	/*
	 * Yeah, it's the switching-frequency, could means many wakee or
	 * rapidly switch, use factor here will just help to automatically
	 * adjust the loose-degree, so bigger node will lead to more pull.
	 */
	if (p->wakee_flips > factor) {
		/*
		 * wakee is somewhat hot, it needs certain amount of cpu
		 * resource, so if waker is far more hot, prefer to leave
		 * it alone.
		 */
		if (current->wakee_flips > (factor * p->wakee_flips))
			return 1;
	}

	return 0;
}

/*********************************************************************************************************
** 函数名称: wake_affine
** 功能描述: 用来判断指定的被唤醒任务是否可以在当前执行唤醒操作的 cpu 上运行
** 输	 入: sd - 指定的 affine 调度域指针
**         : p - 指定的被唤醒任务指针
**         : sync - 是否设置了 WF_SYNC
** 输	 出: 1 - 被唤醒任务可以在当前 cpu 上运行
**         : 0 - 被唤醒任务不可以在当前 cpu 上运行
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int wake_affine(struct sched_domain *sd, struct task_struct *p, int sync)
{
	s64 this_load, load;
	s64 this_eff_load, prev_eff_load;
	int idx, this_cpu, prev_cpu;
	struct task_group *tg;
	unsigned long weight;
	int balanced;

	/*
	 * If we wake multiple tasks be careful to not bounce
	 * ourselves around too much.
	 */
	if (wake_wide(p))
		return 0;

	idx	  = sd->wake_idx;
	this_cpu  = smp_processor_id();
	prev_cpu  = task_cpu(p);
	load	  = source_load(prev_cpu, idx);
	this_load = target_load(this_cpu, idx);

	/*
	 * If sync wakeup then subtract the (maximum possible)
	 * effect of the currently running task from the load
	 * of the current CPU:
	 */
	if (sync) {
		tg = task_group(current);
		weight = current->se.load.weight;

		this_load += effective_load(tg, this_cpu, -weight, -weight);
		load += effective_load(tg, prev_cpu, 0, -weight);
	}

	tg = task_group(p);
	weight = p->se.load.weight;

	/*
	 * In low-load situations, where prev_cpu is idle and this_cpu is idle
	 * due to the sync cause above having dropped this_load to 0, we'll
	 * always have an imbalance, but there's really nothing you can do
	 * about that, so that's good too.
	 *
	 * Otherwise check if either cpus are near enough in load to allow this
	 * task to be woken on this_cpu.
	 */
	this_eff_load = 100;
	this_eff_load *= capacity_of(prev_cpu);

	prev_eff_load = 100 + (sd->imbalance_pct - 100) / 2;
	prev_eff_load *= capacity_of(this_cpu);

	if (this_load > 0) {
		this_eff_load *= this_load +
			effective_load(tg, this_cpu, weight, weight);

		prev_eff_load *= load + effective_load(tg, prev_cpu, 0, weight);
	}

    /* 如果当前 cpu 负载比 prev cpu 的负载低了指定的阈值范围，则表示被唤醒的任务可以
       在当前执行唤醒操作的 cpu 上运行 */
	balanced = this_eff_load <= prev_eff_load;

	schedstat_inc(p, se.statistics.nr_wakeups_affine_attempts);

	if (!balanced)
		return 0;

	schedstat_inc(sd, ttwu_move_affine);
	schedstat_inc(p, se.statistics.nr_wakeups_affine);

	return 1;
}

/*
 * find_idlest_group finds and returns the least busy CPU group within the
 * domain.
 */
/*********************************************************************************************************
** 函数名称: find_idlest_group
** 功能描述: 为指定的任务在指定的调度域中找到一个负载最低的调度组
** 输	 入: sd - 指定的调度域指针
**         : p - 指定的任务指针
**         : this_cpu - 当前正在执行的 cpu id
**         : sd_flag - 指定的调度域 flags，例如 SD_BALANCE_WAKE
** 输	 出: idlest - 调度组指针
**         : NULL - 没找到匹配的调度组
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct sched_group *
find_idlest_group(struct sched_domain *sd, struct task_struct *p,
		  int this_cpu, int sd_flag)
{
	struct sched_group *idlest = NULL, *group = sd->groups;
	unsigned long min_load = ULONG_MAX, this_load = 0;
	int load_idx = sd->forkexec_idx;
	int imbalance = 100 + (sd->imbalance_pct-100)/2;

	if (sd_flag & SD_BALANCE_WAKE)
		load_idx = sd->wake_idx;

	do {
		unsigned long load, avg_load;
		int local_group;
		int i;

		/* Skip over this group if it has no CPUs allowed */
		if (!cpumask_intersects(sched_group_cpus(group),
					tsk_cpus_allowed(p)))
			continue;

		local_group = cpumask_test_cpu(this_cpu,
					       sched_group_cpus(group));

		/* Tally up the load of all CPUs in the group */
		avg_load = 0;

		for_each_cpu(i, sched_group_cpus(group)) {
			/* Bias balancing toward cpus of our domain */
			if (local_group)
				load = source_load(i, load_idx);
			else
				load = target_load(i, load_idx);

			avg_load += load;
		}

		/* Adjust by relative CPU capacity of the group */
		avg_load = (avg_load * SCHED_CAPACITY_SCALE) / group->sgc->capacity;

		if (local_group) {
			this_load = avg_load;
		} else if (avg_load < min_load) {
			min_load = avg_load;
			idlest = group;
		}
	} while (group = group->next, group != sd->groups);

	if (!idlest || 100*this_load < imbalance*min_load)
		return NULL;
	return idlest;
}

/*
 * find_idlest_cpu - find the idlest cpu among the cpus in group.
 */
/*********************************************************************************************************
** 函数名称: find_idlest_cpu
** 功能描述: 为指定的任务在指定的调度组中找到一个负载最低的 cpu
** 输	 入: group - 指定的调度组指针
**         : p - 指定的任务指针
**         : this_cpu - 当前正在执行的 cpu id
** 输	 出: idlest - 调度组指针
**         : NULL - 没找到匹配的调度组
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int
find_idlest_cpu(struct sched_group *group, struct task_struct *p, int this_cpu)
{
	unsigned long load, min_load = ULONG_MAX;
	unsigned int min_exit_latency = UINT_MAX;
	u64 latest_idle_timestamp = 0;
	int least_loaded_cpu = this_cpu;
	int shallowest_idle_cpu = -1;
	int i;

	/* Traverse only the allowed CPUs */
	for_each_cpu_and(i, sched_group_cpus(group), tsk_cpus_allowed(p)) {
		if (idle_cpu(i)) {
			struct rq *rq = cpu_rq(i);
			struct cpuidle_state *idle = idle_get_state(rq);
			if (idle && idle->exit_latency < min_exit_latency) {
				/*
				 * We give priority to a CPU whose idle state
				 * has the smallest exit latency irrespective
				 * of any idle timestamp.
				 */
				min_exit_latency = idle->exit_latency;
				latest_idle_timestamp = rq->idle_stamp;
				shallowest_idle_cpu = i;
			} else if ((!idle || idle->exit_latency == min_exit_latency) &&
				   rq->idle_stamp > latest_idle_timestamp) {
				/*
				 * If equal or no active idle state, then
				 * the most recently idled CPU might have
				 * a warmer cache.
				 */
				latest_idle_timestamp = rq->idle_stamp;
				shallowest_idle_cpu = i;
			}
		} else if (shallowest_idle_cpu == -1) {
			load = weighted_cpuload(i);
			if (load < min_load || (load == min_load && i == this_cpu)) {
				min_load = load;
				least_loaded_cpu = i;
			}
		}
	}

	return shallowest_idle_cpu != -1 ? shallowest_idle_cpu : least_loaded_cpu;
}

/*
 * Try and locate an idle CPU in the sched_domain.
 */
/*********************************************************************************************************
** 函数名称: select_idle_sibling
** 功能描述: 尝试为指定的任务在指定的 cpu 所属调度域中查找一个空闲 cpu
** 输	 入: p - 指定的任务指针
**         : target - 指定的 cpu id
** 输	 出: target - 空闲的 cpu id
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int select_idle_sibling(struct task_struct *p, int target)
{
	struct sched_domain *sd;
	struct sched_group *sg;
	int i = task_cpu(p);

	if (idle_cpu(target))
		return target;

	/*
	 * If the prevous cpu is cache affine and idle, don't be stupid.
	 */
	if (i != target && cpus_share_cache(i, target) && idle_cpu(i))
		return i;

	/*
	 * Otherwise, iterate the domains and find an elegible idle cpu.
	 */
	sd = rcu_dereference(per_cpu(sd_llc, target));
	for_each_lower_domain(sd) {
		sg = sd->groups;
		do {
			/* 如果指定的任务不能在当前遍历到的调度组上运行，则直接遍历下一个调度组 */
			if (!cpumask_intersects(sched_group_cpus(sg),
						tsk_cpus_allowed(p)))
				goto next;

            /* 通过 (i == target || !idle_cpu(i)) 条件可以逐渐缩小目的范围 */
			for_each_cpu(i, sched_group_cpus(sg)) {
				if (i == target || !idle_cpu(i))
					goto next;
			}

			target = cpumask_first_and(sched_group_cpus(sg),
					tsk_cpus_allowed(p));
			goto done;
next:
			sg = sg->next;
		} while (sg != sd->groups);
	}
done:
	return target;
}

/*
 * select_task_rq_fair: Select target runqueue for the waking task in domains
 * that have the 'sd_flag' flag set. In practice, this is SD_BALANCE_WAKE,
 * SD_BALANCE_FORK, or SD_BALANCE_EXEC.
 *
 * Balances load by selecting the idlest cpu in the idlest group, or under
 * certain conditions an idle sibling cpu if the domain has SD_WAKE_AFFINE set.
 *
 * Returns the target cpu number.
 *
 * preempt must be disabled.
 */
/*********************************************************************************************************
** 函数名称: select_task_rq_fair
** 功能描述: 根据函数指定的参数为指定的被唤醒的任务选择本次运行目标 cpu 
** 输	 入: p - 指定的被唤醒任务指针
**         : prev_cpu - 上次运行所在 cpu id 值
**         : sd_flag - 指定的调度域 flags，例如 SD_BALANCE_WAKE
**         : wake_flags - 指定的唤醒 flags，例如 WF_SYNC
** 输	 出: new_cpu - 本次运行所选择的 cpu id 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int
select_task_rq_fair(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags)
{
	struct sched_domain *tmp, *affine_sd = NULL, *sd = NULL;
	int cpu = smp_processor_id();
	int new_cpu = cpu;
	int want_affine = 0;
	int sync = wake_flags & WF_SYNC;

	if (sd_flag & SD_BALANCE_WAKE)
		want_affine = cpumask_test_cpu(cpu, tsk_cpus_allowed(p));

	rcu_read_lock();

	/* 尝试为指定的任务找到一个亲和性调度域并记录最靠近 cpu 的同属性调度域指针 */
	for_each_domain(cpu, tmp) {
		if (!(tmp->flags & SD_LOAD_BALANCE))
			continue;

		/*
		 * If both cpu and prev_cpu are part of this domain,
		 * cpu is a valid SD_WAKE_AFFINE target.
		 */
		if (want_affine && (tmp->flags & SD_WAKE_AFFINE) &&
		    cpumask_test_cpu(prev_cpu, sched_domain_span(tmp))) {
			affine_sd = tmp;
			break;
		}

		if (tmp->flags & sd_flag)
			sd = tmp;
	}

    /* 如果满足唤醒亲和性条件，则设置 prev_cpu = cpu */
	if (affine_sd && cpu != prev_cpu && wake_affine(affine_sd, p, sync))
		prev_cpu = cpu;

	if (sd_flag & SD_BALANCE_WAKE) {
		new_cpu = select_idle_sibling(p, prev_cpu);
		goto unlock;
	}

	while (sd) {
		struct sched_group *group;
		int weight;

        /* 跳过没有相同标志位的调度域 */
		if (!(sd->flags & sd_flag)) {
			sd = sd->child;
			continue;
		}
       
        /* 跳过没有空闲调度组的调度域 */
		group = find_idlest_group(sd, p, cpu, sd_flag);
		if (!group) {
			sd = sd->child;
			continue;
		}

        /* 跳过没有空闲 cpu 的调度域或者空闲 cpu 等于当前正在运行的 cpu 的调度域 */
		new_cpu = find_idlest_cpu(group, p, cpu);
		if (new_cpu == -1 || new_cpu == cpu) {
			/* Now try balancing at a lower domain level of cpu */
			sd = sd->child;
			continue;
		}

		/* Now try balancing at a lower domain level of new_cpu */
		cpu = new_cpu;
		weight = sd->span_weight;
		sd = NULL;
		for_each_domain(cpu, tmp) {
			if (weight <= tmp->span_weight)
				break;
			if (tmp->flags & sd_flag)
				sd = tmp;
		}
		/* while loop will break here if sd == NULL */
	}
unlock:
	rcu_read_unlock();

	return new_cpu;
}

/*
 * Called immediately before a task is migrated to a new cpu; task_cpu(p) and
 * cfs_rq_of(p) references at time of call are still valid and identify the
 * previous cpu.  However, the caller only guarantees p->pi_lock is held; no
 * other assumptions, including the state of rq->lock, should be made.
 */
/*********************************************************************************************************
** 函数名称: migrate_task_rq_fair
** 功能描述: 在任务迁移前调用，用来同步更新任务对其所在 cfs 运行队列的无需衰减的负载贡献值
** 输	 入: p - 指定的迁移任务指针
**         : next_cpu - 指定的迁移目的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
migrate_task_rq_fair(struct task_struct *p, int next_cpu)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	/*
	 * Load tracking: accumulate removed load so that it can be processed
	 * when we next update owning cfs_rq under rq->lock.  Tasks contribute
	 * to blocked load iff they have a positive decay-count.  It can never
	 * be negative here since on-rq tasks have decay-count == 0.
	 */
	if (se->avg.decay_count) {
		se->avg.decay_count = -__synchronize_entity_decay(se);
		atomic_long_add(se->avg.load_avg_contrib,
						&cfs_rq->removed_load);
	}

	/* We have migrated, no longer consider this task hot */
	se->exec_start = 0;
}
#endif /* CONFIG_SMP */

/*********************************************************************************************************
** 函数名称: wakeup_gran
** 功能描述: 计算指定的调度实例的唤醒粒度对应的虚拟运行时间
** 输	 入: curr - 当前 cpu 正在运行的调度实例指针
**         : se - 指定的调度实例指针
** 输	 出: unsigned long - 唤醒粒度虚拟运行时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long
wakeup_gran(struct sched_entity *curr, struct sched_entity *se)
{
	unsigned long gran = sysctl_sched_wakeup_granularity;

	/*
	 * Since its curr running now, convert the gran from real-time
	 * to virtual-time in his units.
	 *
	 * By using 'se' instead of 'curr' we penalize light tasks, so
	 * they get preempted easier. That is, if 'se' < 'curr' then
	 * the resulting gran will be larger, therefore penalizing the
	 * lighter, if otoh 'se' > 'curr' then the resulting gran will
	 * be smaller, again penalizing the lighter task.
	 *
	 * This is especially important for buddies when the leftmost
	 * task is higher priority than the buddy.
	 */
	return calc_delta_fair(gran, se);
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */
/*********************************************************************************************************
** 函数名称: wakeup_preempt_entity
** 功能描述: 判断被唤醒的指定的 se 调度实例是否可以抢占指定的 curr 调度实例
** 输	 入: curr - 当前 cpu 正在运行的调度实例指针
**         : se - 指定的调度实例指针
** 输	 出: 1 - 可以抢占
**         : 0 - 因为唤醒粒度时间不足导致不可以抢占
**         : -1 - 因为虚拟时间太大导致不可以抢占
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff <= 0)
		return -1;

    /* 判断是否达到唤醒粒度 */
	gran = wakeup_gran(curr, se);
	if (vdiff > gran)
		return 1;

	return 0;
}

/*********************************************************************************************************
** 函数名称: set_last_buddy
** 功能描述: 把指定的调度实例到所属任务组树根节点路径成员的 struct cfs_rq.last 设置为指定的调度实例
** 输	 入: se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void set_last_buddy(struct sched_entity *se)
{
	if (entity_is_task(se) && unlikely(task_of(se)->policy == SCHED_IDLE))
		return;

	for_each_sched_entity(se)
		cfs_rq_of(se)->last = se;
}

/*********************************************************************************************************
** 函数名称: set_next_buddy
** 功能描述: 把指定的调度实例到所属任务组树根节点路径成员的 struct cfs_rq.next 设置为指定的调度实例
** 输	 入: se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void set_next_buddy(struct sched_entity *se)
{
	if (entity_is_task(se) && unlikely(task_of(se)->policy == SCHED_IDLE))
		return;

	for_each_sched_entity(se)
		cfs_rq_of(se)->next = se;
}

/*********************************************************************************************************
** 函数名称: set_skip_buddy
** 功能描述: 把指定的调度实例到所属任务组树根节点路径成员的 struct cfs_rq.skip 设置为指定的调度实例
** 输	 入: se - 指定的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void set_skip_buddy(struct sched_entity *se)
{
	for_each_sched_entity(se)
		cfs_rq_of(se)->skip = se;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
/*********************************************************************************************************
** 函数名称: check_preempt_wakeup
** 功能描述: 判断在指定的 cpu 运行队列中被唤醒的指定任务是否可以抢占当前正在执行的任务，如果可以
**         : 则执行任务抢占操作
** 输	 入: rq - 指定的 cpu 运行队列
**         : p - 指定的被唤醒任务指针
**         : wake_flags - 指定的唤醒 flags，例如 WF_FORK
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p, int wake_flags)
{
	struct task_struct *curr = rq->curr;
	struct sched_entity *se = &curr->se, *pse = &p->se;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	int scale = cfs_rq->nr_running >= sched_nr_latency;
	int next_buddy_marked = 0;

	if (unlikely(se == pse))
		return;

	/*
	 * This is possible from callers such as attach_tasks(), in which we
	 * unconditionally check_prempt_curr() after an enqueue (which may have
	 * lead to a throttle).  This both saves work and prevents false
	 * next-buddy nomination below.
	 */
	if (unlikely(throttled_hierarchy(cfs_rq_of(pse))))
		return;

	if (sched_feat(NEXT_BUDDY) && scale && !(wake_flags & WF_FORK)) {
		set_next_buddy(pse);
		next_buddy_marked = 1;
	}

	/*
	 * We can come here with TIF_NEED_RESCHED already set from new task
	 * wake up path.
	 *
	 * Note: this also catches the edge-case of curr being in a throttled
	 * group (e.g. via set_curr_task), since update_curr() (in the
	 * enqueue of curr) will have resulted in resched being set.  This
	 * prevents us from potentially nominating it as a false LAST_BUDDY
	 * below.
	 */
	if (test_tsk_need_resched(curr))
		return;

	/* Idle tasks are by definition preempted by non-idle tasks. */
	if (unlikely(curr->policy == SCHED_IDLE) &&
	    likely(p->policy != SCHED_IDLE))
		goto preempt;

	/*
	 * Batch and idle tasks do not preempt non-idle tasks (their preemption
	 * is driven by the tick):
	 */
	if (unlikely(p->policy != SCHED_NORMAL) || !sched_feat(WAKEUP_PREEMPTION))
		return;

	find_matching_se(&se, &pse);
	update_curr(cfs_rq_of(se));
	BUG_ON(!pse);
	if (wakeup_preempt_entity(se, pse) == 1) {
		/*
		 * Bias pick_next to pick the sched entity that is
		 * triggering this preemption.
		 */
		if (!next_buddy_marked)
			set_next_buddy(pse);
		goto preempt;
	}

	return;

preempt:
	resched_curr(rq);
	/*
	 * Only set the backward buddy when the current task is still
	 * on the rq. This can happen when a wakeup gets interleaved
	 * with schedule on the ->pre_schedule() or idle_balance()
	 * point, either of which can * drop the rq lock.
	 *
	 * Also, during early boot the idle thread is in the fair class,
	 * for obvious reasons its a bad idea to schedule back to it.
	 */
	if (unlikely(!se->on_rq || curr == rq->idle))
		return;

	if (sched_feat(LAST_BUDDY) && scale && entity_is_task(se))
		set_last_buddy(se);
}

/*********************************************************************************************************
** 函数名称: pick_next_task_fair
** 功能描述: 把指定的当前正在运行的调度实例放到所属 cfs 运行队列中并选择一个新的调度实例并设置为 current
** 注     释: 如果我们指定的调度实例 prev 仍然是系统内最需要运行的任务，则会重新选择并运行它
** 输	 入: rq - 指定的 cpu 运行队列
**         : prev - 指定的当前正在运行的调度实例指针
** 输	 出: p - 新的 current 任务指针
**         : NULL - 执行失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct task_struct *
pick_next_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;
	struct task_struct *p;
	int new_tasks;

again:
#ifdef CONFIG_FAIR_GROUP_SCHED
	if (!cfs_rq->nr_running)
		goto idle;

	if (prev->sched_class != &fair_sched_class)
		goto simple;

	/*
	 * Because of the set_next_buddy() in dequeue_task_fair() it is rather
	 * likely that a next task is from the same cgroup as the current.
	 *
	 * Therefore attempt to avoid putting and setting the entire cgroup
	 * hierarchy, only change the part that actually changes.
	 */

	do {
		struct sched_entity *curr = cfs_rq->curr;

		/*
		 * Since we got here without doing put_prev_entity() we also
		 * have to consider cfs_rq->curr. If it is still a runnable
		 * entity, update_curr() will update its vruntime, otherwise
		 * forget we've ever seen it.
		 */
		if (curr && curr->on_rq)
			update_curr(cfs_rq);
		else
			curr = NULL;

		/*
		 * This call to check_cfs_rq_runtime() will do the throttle and
		 * dequeue its entity in the parent(s). Therefore the 'simple'
		 * nr_running test will indeed be correct.
		 */
		if (unlikely(check_cfs_rq_runtime(cfs_rq)))
			goto simple;

        /* 如果当前选择的调度实例是任务组，则递归向下选择，直到选择的调度实例为任务 */
		se = pick_next_entity(cfs_rq, curr);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

    /* p 表示当前选择的，待运行的任务指针 */
	p = task_of(se);

	/*
	 * Since we haven't yet done put_prev_entity and if the selected task
	 * is a different task than we started out with, try and touch the
	 * least amount of cfs_rqs.
	 */
	if (prev != p) {
		struct sched_entity *pse = &prev->se;

        /* 如果任务切换前后，两个任务在任务组中的深度不同，则需要先把他们在
	       任务组树上的深度调整一致 */
		while (!(cfs_rq = is_same_group(se, pse))) {
			int se_depth = se->depth;
			int pse_depth = pse->depth;

			if (se_depth <= pse_depth) {
				put_prev_entity(cfs_rq_of(pse), pse);
				pse = parent_entity(pse);
			}
			if (se_depth >= pse_depth) {
				set_next_entity(cfs_rq_of(se), se);
				se = parent_entity(se);
			}
		}

        /* 在任务切换前后，两个任务在任务组中的深度调整为一致的情况下，把之前的 current
		   放到 cfs 运行队列的红黑树上，把待运行的任务从红黑树上取下并设置为 current */
		put_prev_entity(cfs_rq, pse);
		set_next_entity(cfs_rq, se);
	}

	if (hrtick_enabled(rq))
		hrtick_start_fair(rq, p);

	return p;
simple:
	cfs_rq = &rq->cfs;
#endif

	if (!cfs_rq->nr_running)
		goto idle;

    /* 把指定的当前正在运行的调度实例放回到所属 cpu 运行队列的上并更新相关调度统计值 */
	put_prev_task(rq, prev);

	/* 如果指定的调度实例是任务组，则递归向下选择，直到选择的调度实例为任务 */
	do {
		se = pick_next_entity(cfs_rq, NULL);
		set_next_entity(cfs_rq, se);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	p = task_of(se);

	if (hrtick_enabled(rq))
		hrtick_start_fair(rq, p);

	return p;

idle:
	new_tasks = idle_balance(rq);
	/*
	 * Because idle_balance() releases (and re-acquires) rq->lock, it is
	 * possible for any higher priority task to appear. In that case we
	 * must re-start the pick_next_entity() loop.
	 */
	if (new_tasks < 0)
		return RETRY_TASK;

	if (new_tasks > 0)
		goto again;

	return NULL;
}

/*
 * Account for a descheduled task:
 */
/*********************************************************************************************************
** 函数名称: put_prev_task_fair
** 功能描述: 把指定的当前正在运行的调度实例按照虚拟运行时间添加到所属 cfs 运行队列的红黑树上并更新
**         : 相关调度统计值
** 输	 入: rq - 指定的 cpu 运行队列
**         : prev - 指定的当前正在运行的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq;

    /* 如果指定的调度实例是任务组，则需要处理指定的任务组到树根节点路径
       上的所有任务组调度实例 */
	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		put_prev_entity(cfs_rq, se);
	}
}

/*
 * sched_yield() is very simple
 *
 * The magic of dealing with the ->skip buddy is in pick_next_entity.
 */
/*********************************************************************************************************
** 函数名称: yield_task_fair
** 功能描述: 尝试清除指定的 cpu 运行队列和当前正在运行的 cfs 调度实例相关的 buddies 信息并把
**         : 当前正在运行的 cfs 调度实例设置为这个 cfs 运行队列的 skip 成员
** 输	 入: rq - 指定的 cpu 运行队列
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void yield_task_fair(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *se = &curr->se;

	/*
	 * Are we the only task in the tree?
	 */
	if (unlikely(rq->nr_running == 1))
		return;

	clear_buddies(cfs_rq, se);

	if (curr->policy != SCHED_BATCH) {
		update_rq_clock(rq);
		/*
		 * Update run-time statistics of the 'current'.
		 */
		update_curr(cfs_rq);
		/*
		 * Tell update_rq_clock() that we've just updated,
		 * so we don't do microscopic update in schedule()
		 * and double the fastpath cost.
		 */
		rq_clock_skip_update(rq, true);
	}

	set_skip_buddy(se);
}

/*********************************************************************************************************
** 函数名称: yield_to_task_fair
** 功能描述: 把指定的任务设置为指定的 cpu 运行队列的 cfs 运行队列的 next 成员，并把当前正在运行的 cfs 
**         : 调度实例设置为 cfs 运行队列的 skip 成员
** 注     释: 执行完这个函数之后，指定的任务只是被设置为 cfs 运行队列的 next 成员，这个时候还没有运行它
** 输	 入: rq - 指定的 cpu 运行队列
**         : p - 指定的待运行的任务指针
**         : preempt - 未使用
** 输	 出: true - 执行成功
**         : false - 执行失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool yield_to_task_fair(struct rq *rq, struct task_struct *p, bool preempt)
{
	struct sched_entity *se = &p->se;

	/* throttled hierarchies are not runnable */
	if (!se->on_rq || throttled_hierarchy(cfs_rq_of(se)))
		return false;

	/* Tell the scheduler that we'd really like pse to run next. */
	set_next_buddy(se);

    /* 尝试清除指定的 cpu 运行队列和当前正在运行的 cfs 调度实例相关的 buddies 信息并把
       当前正在运行的 cfs 调度实例设置为这个 cfs 运行队列的 skip 成员 */
	yield_task_fair(rq);

	return true;
}

#ifdef CONFIG_SMP
/**************************************************
 * Fair scheduling class load-balancing methods.
 *
 * BASICS
 *
 * The purpose of load-balancing is to achieve the same basic fairness the
 * per-cpu scheduler provides, namely provide a proportional amount of compute
 * time to each task. This is expressed in the following equation:
 *
 *   W_i,n/P_i == W_j,n/P_j for all i,j                               (1)
 *
 * Where W_i,n is the n-th weight average for cpu i. The instantaneous weight
 * W_i,0 is defined as:
 *
 *   W_i,0 = \Sum_j w_i,j                                             (2)
 *
 * Where w_i,j is the weight of the j-th runnable task on cpu i. This weight
 * is derived from the nice value as per prio_to_weight[].
 *
 * The weight average is an exponential decay average of the instantaneous
 * weight:
 *
 *   W'_i,n = (2^n - 1) / 2^n * W_i,n + 1 / 2^n * W_i,0               (3)
 *
 * C_i is the compute capacity of cpu i, typically it is the
 * fraction of 'recent' time available for SCHED_OTHER task execution. But it
 * can also include other factors [XXX].
 *
 * To achieve this balance we define a measure of imbalance which follows
 * directly from (1):
 *
 *   imb_i,j = max{ avg(W/C), W_i/C_i } - min{ avg(W/C), W_j/C_j }    (4)
 *
 * We them move tasks around to minimize the imbalance. In the continuous
 * function space it is obvious this converges, in the discrete case we get
 * a few fun cases generally called infeasible weight scenarios.
 *
 * [XXX expand on:
 *     - infeasible weights;
 *     - local vs global optima in the discrete case. ]
 *
 *
 * SCHED DOMAINS
 *
 * In order to solve the imbalance equation (4), and avoid the obvious O(n^2)
 * for all i,j solution, we create a tree of cpus that follows the hardware
 * topology where each level pairs two lower groups (or better). This results
 * in O(log n) layers. Furthermore we reduce the number of cpus going up the
 * tree to only the first of the previous level and we decrease the frequency
 * of load-balance at each level inv. proportional to the number of cpus in
 * the groups.
 *
 * This yields:
 *
 *     log_2 n     1     n
 *   \Sum       { --- * --- * 2^i } = O(n)                            (5)
 *     i = 0      2^i   2^i
 *                               `- size of each group
 *         |         |     `- number of cpus doing load-balance
 *         |         `- freq
 *         `- sum over all levels
 *
 * Coupled with a limit on how many tasks we can migrate every balance pass,
 * this makes (5) the runtime complexity of the balancer.
 *
 * An important property here is that each CPU is still (indirectly) connected
 * to every other cpu in at most O(log n) steps:
 *
 * The adjacency matrix of the resulting graph is given by:
 *
 *             log_2 n     
 *   A_i,j = \Union     (i % 2^k == 0) && i / 2^(k+1) == j / 2^(k+1)  (6)
 *             k = 0
 *
 * And you'll find that:
 *
 *   A^(log_2 n)_i,j != 0  for all i,j                                (7)
 *
 * Showing there's indeed a path between every cpu in at most O(log n) steps.
 * The task movement gives a factor of O(m), giving a convergence complexity
 * of:
 *
 *   O(nm log n),  n := nr_cpus, m := nr_tasks                        (8)
 *
 *
 * WORK CONSERVING
 *
 * In order to avoid CPUs going idle while there's still work to do, new idle
 * balancing is more aggressive and has the newly idle cpu iterate up the domain
 * tree itself instead of relying on other CPUs to bring it work.
 *
 * This adds some complexity to both (5) and (8) but it reduces the total idle
 * time.
 *
 * [XXX more?]
 *
 *
 * CGROUPS
 *
 * Cgroups make a horror show out of (2), instead of a simple sum we get:
 *
 *                                s_k,i
 *   W_i,0 = \Sum_j \Prod_k w_k * -----                               (9)
 *                                 S_k
 *
 * Where
 *
 *   s_k,i = \Sum_j w_i,j,k  and  S_k = \Sum_i s_k,i                 (10)
 *
 * w_i,j,k is the weight of the j-th runnable task in the k-th cgroup on cpu i.
 *
 * The big problem is S_k, its a global sum needed to compute a local (W_i)
 * property.
 *
 * [XXX write more on how we solve this.. _after_ merging pjt's patches that
 *      rewrite all of this once again.]
 */ 

static unsigned long __read_mostly max_load_balance_interval = HZ/10;

/* fbq - find busiest queue
   xxx->sum_nr_running       - 表示当前组/运行队列中实际运行的调度实例个数
   xxx->nr_numa_running      - 表示当前组/运行队列中分配了 preferred numa id 的调度实例个数
   xxx->nr_preferred_running - 表示当前组/运行队列中分配的 preferred numa id 等于当前 cpu 的
                               numa id 的调度实例个数，即运行在理想 numa node 上的调度实例个数

   当前 linux 系统将组/运行队列分成三类，分别是 regular, remote, all，他们的具体含义如下：
   regular - xxx->sum_nr_running > xxx->nr_numa_running，表示当前组/运行队列中包含非 numa 调度实例，即未
             分配 preferred numa id 的调度实例

   remote  - xxx->sum_nr_running > xxx->nr_preferred_running && xxx->sum_nr_running < xxx->nr_numa_running
             表示当前组/运行队列中包含没有运行在理想 numa node 上的调度实例，即 preferred numa id 不等于
             当前 cpu 的 numa id 的调度实例，在负载均衡迁移中，可以优先迁移这种调度实例

   all     - xxx->sum_nr_running < xxx->nr_preferred_running，表示没有什么区别 */
enum fbq_type { regular, remote, all };

/* LBF - load balance flags */
/* 表示在负载均衡任务迁移过程中，因为指定的过载 cpu 运行队列上的所有任务都因为
   cpu 亲和力原因导致不能迁移到指定的目的 cpu 上，详情见 load_balance 函数和
   can_migrate_task 函数 */
#define LBF_ALL_PINNED	0x01

/* 表示在负载均衡任务迁移过程中，因为本次迁移的任务数已经超过设定的阈值而退出
   详情见 detach_tasks 函数 */
#define LBF_NEED_BREAK	0x02

/* 表示在负载均衡任务迁移过程中，因为需要被迁移的任务因为 cpu 亲和力不能迁移到
   指定的目的 cpu 上，但是这个任务可以迁移到指定的目的 cpu 所属组的其它 cpu 上
   所以，我们可以通过把这个任务迁移到这个这个任务组的其他 cpu 上调节当前存在的
   负载失衡，详情见 can_migrate_task 函数 */
#define LBF_DST_PINNED  0x04

/* 表示在负载均衡任务迁移过程中，因为迁移任务的亲和力导致不能迁移到目的 cpu 上
   详情见 can_migrate_task 函数 */
#define LBF_SOME_PINNED	0x08

struct lb_env {
    /* 表示当前负载均衡环境所属调度域指针 */
	struct sched_domain	*sd;

    /* 表示当前负载均衡环境的源 cpu 运行队列和源 cpu id */
	struct rq		*src_rq;
	int			     src_cpu;

    /* 表示当前负载均衡环境的目的 cpu 运行队列和目的 cpu id */
	int			     dst_cpu;
	struct rq		*dst_rq;

    /* 表示当前目的调度组包含的 cpu 掩码值，这个变量是 struct lb_env.cpus 的子集 */
	struct cpumask  *dst_grpmask;

	/* 表示在负载均衡任务迁移过程中，因为需要被迁移的任务因为 cpu 亲和力不能迁移到
	   指定的目的 cpu 上，但是这个任务可以迁移到指定的目的 cpu 所属组的其它 cpu 上
	   所以，我们可以通过把这个任务迁移到这个这个任务组的其他 cpu 上调节当前存在的
	   负载失衡，new_dst_cpu 表示这个任务组的其他 cpu，详情见 can_migrate_task 函数 */
	int			new_dst_cpu;

	/* 表示当前正在运行的 cpu 的 idle 类型 */
	enum cpu_idle_type	idle;

	/* 表示当前负载均衡环境一共需要平衡的负载量（即已经失衡的负载量）
	   详情见 check_asym_packing 和 fix_small_imbalance 函数 */
	long			imbalance;
	
	/* The set of CPUs under consideration for load-balancing */
	/* 当前负载均衡环境可以用来执行负载均衡的所有目的 cpu 候选者掩码值 */
	struct cpumask		*cpus;

	unsigned int		flags;

    /* loop - 表示当前负载均衡环境在 detach 时连续执行的次数
       loop_break - 表示当前负载均衡环境在 detach 时允许连续执行的次数
       loop_max - 表示当前负载均衡环境在 detach 时允许最大连续执行的次数
       详情见 detach_tasks 函数 */
	unsigned int		loop;
	unsigned int		loop_break;
	unsigned int		loop_max;

    /* 表示当前负载均衡环境所属组/运行队列的 fbq 类型 */
	enum fbq_type		fbq_type;

	/* 在执行负载均衡操作时，会把源 cpu 运行队列的任务放到这个链表上，详情
	   见 detach_tasks 函数，然后在把这个链表上的任务放到目的 cpu 的运行队
	   列上，详情见 attach_tasks 函数 */
	struct list_head	tasks;
};

/*
 * Is this task likely cache-hot:
 */
/*********************************************************************************************************
** 函数名称: task_hot
** 功能描述: 判断指定的任务在指定的负载均衡环境下是否为 code cache hot
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 1 - 是 code cache hot
**         : 0 - 不是 code cache hot
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int task_hot(struct task_struct *p, struct lb_env *env)
{
	s64 delta;

	lockdep_assert_held(&env->src_rq->lock);

	if (p->sched_class != &fair_sched_class)
		return 0;

	if (unlikely(p->policy == SCHED_IDLE))
		return 0;

	/*
	 * Buddy candidates are cache hot:
	 */
	if (sched_feat(CACHE_HOT_BUDDY) && env->dst_rq->nr_running &&
			(&p->se == cfs_rq_of(&p->se)->next ||
			 &p->se == cfs_rq_of(&p->se)->last))
		return 1;

	if (sysctl_sched_migration_cost == -1)
		return 1;
	if (sysctl_sched_migration_cost == 0)
		return 0;

	delta = rq_clock_task(env->src_rq) - p->se.exec_start;

	return delta < (s64)sysctl_sched_migration_cost;
}

#ifdef CONFIG_NUMA_BALANCING
/* Returns true if the destination node has incurred more faults */
/*********************************************************************************************************
** 函数名称: migrate_improves_locality
** 功能描述: 判断把指定的任务从指定的负载均衡环境的源 node 上迁移到目的 node 上是否会提高内存局部访问命中率
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 1 - 会
**         : 0 - 不会
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool migrate_improves_locality(struct task_struct *p, struct lb_env *env)
{
	struct numa_group *numa_group = rcu_dereference(p->numa_group);
	int src_nid, dst_nid;

	if (!sched_feat(NUMA_FAVOUR_HIGHER) || !p->numa_faults ||
	    !(env->sd->flags & SD_NUMA)) {
		return false;
	}

	src_nid = cpu_to_node(env->src_cpu);
	dst_nid = cpu_to_node(env->dst_cpu);

	if (src_nid == dst_nid)
		return false;

	if (numa_group) {
		/* Task is already in the group's interleave set. */
		if (node_isset(src_nid, numa_group->active_nodes))
			return false;

		/* Task is moving into the group's interleave set. */
		if (node_isset(dst_nid, numa_group->active_nodes))
			return true;

		return group_faults(p, dst_nid) > group_faults(p, src_nid);
	}

	/* Encourage migration to the preferred node. */
	if (dst_nid == p->numa_preferred_nid)
		return true;

	return task_faults(p, dst_nid) > task_faults(p, src_nid);
}

/*********************************************************************************************************
** 函数名称: migrate_degrades_locality
** 功能描述: 判断把指定的任务从指定的负载均衡环境的源 node 上迁移到目的 node 上是否会降低内存局部访问命中率
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 1 - 会
**         : 0 - 不会
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool migrate_degrades_locality(struct task_struct *p, struct lb_env *env)
{
	struct numa_group *numa_group = rcu_dereference(p->numa_group);
	int src_nid, dst_nid;

	if (!sched_feat(NUMA) || !sched_feat(NUMA_RESIST_LOWER))
		return false;

	if (!p->numa_faults || !(env->sd->flags & SD_NUMA))
		return false;

	src_nid = cpu_to_node(env->src_cpu);
	dst_nid = cpu_to_node(env->dst_cpu);

	if (src_nid == dst_nid)
		return false;

	if (numa_group) {
		/* Task is moving within/into the group's interleave set. */
		if (node_isset(dst_nid, numa_group->active_nodes))
			return false;

		/* Task is moving out of the group's interleave set. */
		if (node_isset(src_nid, numa_group->active_nodes))
			return true;

		return group_faults(p, dst_nid) < group_faults(p, src_nid);
	}

	/* Migrating away from the preferred node is always bad. */
	if (src_nid == p->numa_preferred_nid)
		return true;

	return task_faults(p, dst_nid) < task_faults(p, src_nid);
}

#else

/*********************************************************************************************************
** 函数名称: migrate_improves_locality
** 功能描述: 判断把指定的任务从指定的负载均衡环境的源 node 上迁移到目的 node 上是否会提高内存局部访问命中率
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 1 - 会
**         : 0 - 不会
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool migrate_improves_locality(struct task_struct *p,
					     struct lb_env *env)
{
	return false;
}

/*********************************************************************************************************
** 函数名称: migrate_degrades_locality
** 功能描述: 判断把指定的任务从指定的负载均衡环境的源 node 上迁移到目的 node 上是否会降低内存局部访问命中率
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 1 - 会
**         : 0 - 不会
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool migrate_degrades_locality(struct task_struct *p,
					     struct lb_env *env)
{
	return false;
}
#endif

/*
 * can_migrate_task - may task p from runqueue rq be migrated to this_cpu?
 */
/*********************************************************************************************************
** 函数名称: can_migrate_task
** 功能描述: 判断指定的任务是否可以从指定的负载均衡环境的源 cpu 上迁移到目的 cpu 上
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 1 - 可以
**         : 0 - 不可以
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static
int can_migrate_task(struct task_struct *p, struct lb_env *env)
{
	int tsk_cache_hot = 0;

	lockdep_assert_held(&env->src_rq->lock);

	/*
	 * We do not migrate tasks that are:
	 * 1) throttled_lb_pair, or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) running (obviously), or
	 * 4) are cache-hot on their current CPU.
	 */
	if (throttled_lb_pair(task_group(p), env->src_cpu, env->dst_cpu))
		return 0;

    /* 如果指定的任务不可以在指定的目的 cpu 上运行，则不可以迁移 */
	if (!cpumask_test_cpu(env->dst_cpu, tsk_cpus_allowed(p))) {
		int cpu;

		schedstat_inc(p, se.statistics.nr_failed_migrations_affine);

		env->flags |= LBF_SOME_PINNED;

		/*
		 * Remember if this task can be migrated to any other cpu in
		 * our sched_group. We may want to revisit it if we couldn't
		 * meet load balance goals by pulling other tasks on src_cpu.
		 *
		 * Also avoid computing new_dst_cpu if we have already computed
		 * one in current iteration.
		 */
		if (!env->dst_grpmask || (env->flags & LBF_DST_PINNED))
			return 0;

		/* Prevent to re-select dst_cpu via env's cpus */
		for_each_cpu_and(cpu, env->dst_grpmask, env->cpus) {
			if (cpumask_test_cpu(cpu, tsk_cpus_allowed(p))) {
				env->flags |= LBF_DST_PINNED;
				env->new_dst_cpu = cpu;
				break;
			}
		}

		return 0;
	}

	/* Record that we found at least one task that could run on dst_cpu */
	env->flags &= ~LBF_ALL_PINNED;

    /* 如果指定的任务正在 cpu 上运行，则不可以迁移 */
	if (task_running(env->src_rq, p)) {
		schedstat_inc(p, se.statistics.nr_failed_migrations_running);
		return 0;
	}

	/*
	 * Aggressive migration if:
	 * 1) destination numa is preferred
	 * 2) task is cache cold, or
	 * 3) too many balance attempts have failed.
	 */
	tsk_cache_hot = task_hot(p, env);
	if (!tsk_cache_hot)
		tsk_cache_hot = migrate_degrades_locality(p, env);

	if (migrate_improves_locality(p, env) || !tsk_cache_hot ||
	    env->sd->nr_balance_failed > env->sd->cache_nice_tries) {
		if (tsk_cache_hot) {
			schedstat_inc(env->sd, lb_hot_gained[env->idle]);
			schedstat_inc(p, se.statistics.nr_forced_migrations);
		}
		return 1;
	}

	schedstat_inc(p, se.statistics.nr_failed_migrations_hot);
	return 0;
}

/*
 * detach_task() -- detach the task for the migration specified in env
 */
/*********************************************************************************************************
** 函数名称: detach_task
** 功能描述: 把指定的任务从指定的负载均衡环境的源 cpu 的运行队列上移除并设置目的 cpu 信息
** 输	 入: p - 指定的任务指针
**         : env - 指定的负载均衡环境指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void detach_task(struct task_struct *p, struct lb_env *env)
{
	lockdep_assert_held(&env->src_rq->lock);

	deactivate_task(env->src_rq, p, 0);
	p->on_rq = TASK_ON_RQ_MIGRATING;
	set_task_cpu(p, env->dst_cpu);
}

/*
 * detach_one_task() -- tries to dequeue exactly one task from env->src_rq, as
 * part of active balancing operations within "domain".
 *
 * Returns a task if successful and NULL otherwise.
 */
/*********************************************************************************************************
** 函数名称: detach_one_task
** 功能描述: 尝试从指定的负载均衡环境的源 cpu 的运行队列上移除一个任务并设置目的 cpu 信息
** 输	 入: env - 指定的负载均衡环境指针
** 输	 出: p - 成功迁移的任务指针
**         : NULL - 迁移失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct task_struct *detach_one_task(struct lb_env *env)
{
	struct task_struct *p, *n;

	lockdep_assert_held(&env->src_rq->lock);

	list_for_each_entry_safe(p, n, &env->src_rq->cfs_tasks, se.group_node) {
		if (!can_migrate_task(p, env))
			continue;

		detach_task(p, env);

		/*
		 * Right now, this is only the second place where
		 * lb_gained[env->idle] is updated (other is detach_tasks)
		 * so we can safely collect stats here rather than
		 * inside detach_tasks().
		 */
		schedstat_inc(env->sd, lb_gained[env->idle]);
		return p;
	}
	return NULL;
}

static const unsigned int sched_nr_migrate_break = 32;

/*
 * detach_tasks() -- tries to detach up to imbalance weighted load from
 * busiest_rq, as part of a balancing operation within domain "sd".
 *
 * Returns number of detached tasks if successful and 0 otherwise.
 */
/*********************************************************************************************************
** 函数名称: detach_tasks
** 功能描述: 尝试从指定的负载均衡环境的源 cpu 的运行队列上移除指定个数的任务并设置目的 cpu 信息，然后
**         : 把成功移除的任务放到指定的负载均衡环境的 struct lb_env.tasks 链表中
** 输	 入: env - 指定的负载均衡环境指针
** 输	 出: detached - 成功 detach 的任务数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int detach_tasks(struct lb_env *env)
{
	struct list_head *tasks = &env->src_rq->cfs_tasks;
	struct task_struct *p;
	unsigned long load;
	int detached = 0;

	lockdep_assert_held(&env->src_rq->lock);

	if (env->imbalance <= 0)
		return 0;

	while (!list_empty(tasks)) {
		p = list_first_entry(tasks, struct task_struct, se.group_node);

		env->loop++;
		/* We've more or less seen every task there is, call it quits */
		if (env->loop > env->loop_max)
			break;

		/* take a breather every nr_migrate tasks (env->loop_break = sched_nr_migrate_break) */
		if (env->loop > env->loop_break) {
			env->loop_break += sched_nr_migrate_break;
			env->flags |= LBF_NEED_BREAK;
			break;
		}

		if (!can_migrate_task(p, env))
			goto next;

		load = task_h_load(p);

		if (sched_feat(LB_MIN) && load < 16 && !env->sd->nr_balance_failed)
			goto next;

		if ((load / 2) > env->imbalance)
			goto next;

		detach_task(p, env);
		list_add(&p->se.group_node, &env->tasks);

		detached++;
		env->imbalance -= load;

#ifdef CONFIG_PREEMPT
		/*
		 * NEWIDLE balancing is a source of latency, so preemptible
		 * kernels will stop after the first task is detached to minimize
		 * the critical section.
		 */
		if (env->idle == CPU_NEWLY_IDLE)
			break;
#endif

		/*
		 * We only want to steal up to the prescribed amount of
		 * weighted load.
		 */
		if (env->imbalance <= 0)
			break;

		continue;
next:
		list_move_tail(&p->se.group_node, tasks);
	}

	/*
	 * Right now, this is one of only two places we collect this stat
	 * so we can safely collect detach_one_task() stats here rather
	 * than inside detach_one_task().
	 */
	schedstat_add(env->sd, lb_gained[env->idle], detached);

	return detached;
}

/*
 * attach_task() -- attach the task detached by detach_task() to its new rq.
 */
/*********************************************************************************************************
** 函数名称: attach_task
** 功能描述: 把指定的任务添加到指定的 cpu 运行队列上并尝试唤醒这个任务
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void attach_task(struct rq *rq, struct task_struct *p)
{
	lockdep_assert_held(&rq->lock);

	BUG_ON(task_rq(p) != rq);
	p->on_rq = TASK_ON_RQ_QUEUED;
	activate_task(rq, p, 0);
	check_preempt_curr(rq, p, 0);
}

/*
 * attach_one_task() -- attaches the task returned from detach_one_task() to
 * its new rq.
 */
/*********************************************************************************************************
** 函数名称: attach_one_task
** 功能描述: 把指定的任务添加到指定的 cpu 的运行队列上并尝试唤醒这个任务
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void attach_one_task(struct rq *rq, struct task_struct *p)
{
	raw_spin_lock(&rq->lock);
	attach_task(rq, p);
	raw_spin_unlock(&rq->lock);
}

/*
 * attach_tasks() -- attaches all tasks detached by detach_tasks() to their
 * new rq.
 */
/*********************************************************************************************************
** 函数名称: attach_tasks
** 功能描述: 把指定的负载均衡环境中的所有 detach 任务添加到指定的目的 cpu 的运行队列上并尝试唤醒它们
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void attach_tasks(struct lb_env *env)
{
	struct list_head *tasks = &env->tasks;
	struct task_struct *p;

	raw_spin_lock(&env->dst_rq->lock);

	while (!list_empty(tasks)) {
		p = list_first_entry(tasks, struct task_struct, se.group_node);
		list_del_init(&p->se.group_node);

		attach_task(env->dst_rq, p);
	}

	raw_spin_unlock(&env->dst_rq->lock);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*
 * update tg->load_weight by folding this cpu's load_avg
 */
/*********************************************************************************************************
** 函数名称: __update_blocked_averages_cpu 
** 功能描述: 更新指定的 cpu 上指定任务组的阻塞负载贡献值以及平均负载贡献值信息
** 输	 入: tg - 指定的任务组指针
**         : cpu - 指定的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __update_blocked_averages_cpu(struct task_group *tg, int cpu)
{
	struct sched_entity *se = tg->se[cpu];
	struct cfs_rq *cfs_rq = tg->cfs_rq[cpu];

	/* throttled entities do not contribute to load */
	if (throttled_hierarchy(cfs_rq))
		return;

	update_cfs_rq_blocked_load(cfs_rq, 1);

	if (se) {
		update_entity_load_avg(se, 1);
		/*
		 * We pivot on our runnable average having decayed to zero for
		 * list removal.  This generally implies that all our children
		 * have also been removed (modulo rounding error or bandwidth
		 * control); however, such cases are rare and we can fix these
		 * at enqueue.
		 *
		 * TODO: fix up out-of-order children on enqueue.
		 */
		if (!se->avg.runnable_avg_sum && !cfs_rq->nr_running)
			list_del_leaf_cfs_rq(cfs_rq);
	} else {
		struct rq *rq = rq_of(cfs_rq);
		update_rq_runnable_avg(rq, rq->nr_running);
	}
}

/*********************************************************************************************************
** 函数名称: update_blocked_averages 
** 功能描述: 更新指定的 cpu 运行队列上每一个任务组叶子节点的 cfs 运行队列的阻塞负载贡献统计值
** 输	 入: cpu - 指定的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_blocked_averages(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct cfs_rq *cfs_rq;
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);
	update_rq_clock(rq);
	/*
	 * Iterates the task_group tree in a bottom up fashion, see
	 * list_add_leaf_cfs_rq() for details.
	 */
	for_each_leaf_cfs_rq(rq, cfs_rq) {
		/*
		 * Note: We may want to consider periodically releasing
		 * rq->lock about these updates so that creating many task
		 * groups does not result in continually extending hold time.
		 */
		__update_blocked_averages_cpu(cfs_rq->tg, rq->cpu);
	}

	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Compute the hierarchical load factor for cfs_rq and all its ascendants.
 * This needs to be done in a top-down fashion because the load of a child
 * group is a fraction of its parents load.
 */
/*********************************************************************************************************
** 函数名称: update_cfs_rq_h_load
** 功能描述: 从任务组中指定的 cfs 运行队列到“根任务组”更新遍历路径成员在任务组树形结构中的加权负载贡献值
** 输	 入: cfs_rq - 指定的任务组 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_cfs_rq_h_load(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);
	struct sched_entity *se = cfs_rq->tg->se[cpu_of(rq)];
	unsigned long now = jiffies;
	unsigned long load;

	if (cfs_rq->last_h_load_update == now)
		return;

	cfs_rq->h_load_next = NULL;

	/* 遍历指定的任务组调度实例到“根任务组”之间遍历路径的每一个任务组调度实例
       因为“根任务组”的 root_task_group->se[cpu_id] = NULL，所以 root_task_group 的所有
       子任务组调度实例的 child_se->parent = NULL，详情见 init_tg_cfs_entry 函数 */
	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);

		/* 设置当前 cfs 运行队列中下一次需要更新 h_load 负载贡献值的调度实例 */
		cfs_rq->h_load_next = se;
		
		if (cfs_rq->last_h_load_update == now)
			break;
	}

    /* 如果 child_se->parent = NULL，此时 cfs_rq 为 root_task_group 的一级子任务组调度节点的 cfs 运行队列 */
	if (!se) {
		cfs_rq->h_load = cfs_rq->runnable_load_avg;
		cfs_rq->last_h_load_update = now;
	}

    /* 从任务组树形结构中，按照从上到下的顺序，根据子任务组负载贡献值占据父任务组负载贡献值
       的比例，顺序更新子任务组的 cfs_rq->h_load 字段值 */
	while ((se = cfs_rq->h_load_next) != NULL) {
		
		/*                        parent_cfs_rq->h_load * child_se->avg.load_avg_contrib
		   child_cfs_rq->h_load = ------------------------------------------------------
		                                  parent_cfs_rq->runnable_load_avg + 1

                                                             child_se->avg.load_avg_contrib
                                = parent_cfs_rq->h_load * ------------------------------------
                                                          parent_cfs_rq->runnable_load_avg + 1 

                                                            child_cfs_rq->runnable_load_avg
                                = parent_cfs_rq->h_load * ------------------------------------
                                                          parent_cfs_rq->runnable_load_avg + 1 */
		load = cfs_rq->h_load;
		load = div64_ul(load * se->avg.load_avg_contrib,
				cfs_rq->runnable_load_avg + 1);
	
		cfs_rq = group_cfs_rq(se);
		cfs_rq->h_load = load;
		cfs_rq->last_h_load_update = now;
	}
}

/*********************************************************************************************************
** 函数名称: task_h_load
** 功能描述: 更新并返回任务组中的指定任务在任务组树形结构中的加权负载贡献值
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned long - 在任务组树形结构中的加权负载贡献值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long task_h_load(struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);

    /* 从任务组中指定的 cfs 运行队列到“根任务组”更新遍历路径成员在任务组树形结构中的加权负载贡献值 */
	update_cfs_rq_h_load(cfs_rq);

	/*                                 p->se.avg.load_avg_contrib
	   task_h_load = cfs_rq->h_load * -----------------------------
	                                  cfs_rq->runnable_load_avg + 1 */
	return div64_ul(p->se.avg.load_avg_contrib * cfs_rq->h_load,
			cfs_rq->runnable_load_avg + 1);
}
#else
/*********************************************************************************************************
** 函数名称: update_blocked_averages 
** 功能描述: 更新指定的 cpu 运行队列上每一个任务组叶子节点的 cfs 运行队列的阻塞负载贡献统计值
** 输	 入: cpu - 指定的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_blocked_averages(int cpu)
{
}

/*********************************************************************************************************
** 函数名称: task_h_load
** 功能描述: 更新并返回任务组中的指定任务在任务组树形结构中的加权负载贡献值
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned long - 在任务组树形结构中的加权负载贡献值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long task_h_load(struct task_struct *p)
{
	return p->se.avg.load_avg_contrib;
}
#endif

/********** Helpers for find_busiest_group ************************/

enum group_type {
	group_other = 0,
	group_imbalanced,
	group_overloaded,
};

/*
 * sg_lb_stats - stats of a sched_group required for load_balancing
 * 详情见 update_sg_lb_stats 函数
 */
struct sg_lb_stats {
	/* 表示的是当前调度组内所有和负载均衡任务迁移匹配的 cpu 在过去一段时间内经过衰减
	   的平均负载贡献值占当前调度组的负载计算能力的百分比（乘以了精度因子），详情见 
	   update_sg_lb_stats 函数，衰减函数见 __update_cpu_load 函数 */
	unsigned long avg_load; /*Avg load across the CPUs of the group */

    /* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 在过去一段时间内经过衰减的负载
       贡献总和，详情见 update_sg_lb_stats 函数，衰减函数见 __update_cpu_load 函数 */
	unsigned long group_load; /* Total load over the CPUs of the group */

	/* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 在过去一段时间内经过衰减的“加权”
	   负载贡献统计值的总和，详情见 update_sg_lb_stats 函数，衰减函数见 update_entity_load_avg 函数 */
	unsigned long sum_weighted_load; /* Weighted load of group's tasks */

	/* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 在过去一段时间内经过衰减的“加权”
	   负载贡献统计值平分到每个任务是多少，详情见 update_sg_lb_stats 函数，衰减函数见
	   update_entity_load_avg 函数 */
	unsigned long load_per_task;

	/* 表示当前调度组的 cpu 在减去实时调度实例运行的时间后，给 cfs 调度实例剩余的负载计算能力 */
	unsigned long group_capacity;

	/* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 上目前实际运行的调度任务个数 */
	unsigned int sum_nr_running; /* Nr tasks running in the group */

	/* 表示当前调度组的负载计算能力可以运行的调度任务个数 */
	unsigned int group_capacity_factor;

	/* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 中空闲 cpu 的个数 */
	unsigned int idle_cpus;

	/* 表示当前调度组的负载权重信息 */
	unsigned int group_weight;
	
	enum group_type group_type;

	/* 表示当前任务组是否包含空闲可用的负载计算能力 */
	int group_has_free_capacity;
	
#ifdef CONFIG_NUMA_BALANCING
    /* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 上已经分配了 preferred_node 的调度实例数 */
	unsigned int nr_numa_running;

	/* 表示当前调度组内所有和负载均衡任务迁移匹配的 cpu 上 preferred_node == task_node 的调度实例数 */
	unsigned int nr_preferred_running;
#endif
};

/*
 * sd_lb_stats - Structure to store the statistics of a sched_domain
 *		 during load balancing.
 */
struct sd_lb_stats {
    /* 表示当前调度域中最忙的调度组 */
	struct sched_group *busiest;	/* Busiest group in this sd */

	/* 表示当前调度域中的本地调度组，本地调度组指定是当前正在运行的 cpu 所属调度组 */
	struct sched_group *local;	/* Local group in this sd */

	/* 表示当前调度域一共贡献的负载统计值 */
	unsigned long total_load;	/* Total load of all groups in sd */

	/* 表示当前调度域的负载计算能力 */
	unsigned long total_capacity;	/* Total capacity of all groups in sd */

	/* 表示的是当前调度域的平均负载贡献值占当前调度域的负载计算能力的百分比（乘以了精度因子）
       详情见 find_busiest_group 函数 */
	unsigned long avg_load;	/* Average load across all groups in sd */

	/* 表示当前调度域内最忙的调度组的负载均衡状态统计数据结构 */
	struct sg_lb_stats busiest_stat;/* Statistics of the busiest group */

	/* 表示当前调度域中的本地调度组（当前 cpu 所在调度组）的负载均衡状态统计数据结构 */
	struct sg_lb_stats local_stat;	/* Statistics of the local group */
};

/*********************************************************************************************************
** 函数名称: init_sd_lb_stats
** 功能描述: 初始化指定的调度域负载均衡状态统计数据结构到默认状态
** 输	 入: sds - 指定的负载均衡状态统计数据结构指针
** 输	 出: sds - 初始化好的负载均衡状态统计数据结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void init_sd_lb_stats(struct sd_lb_stats *sds)
{
	/*
	 * Skimp on the clearing to avoid duplicate work. We can avoid clearing
	 * local_stat because update_sg_lb_stats() does a full clear/assignment.
	 * We must however clear busiest_stat::avg_load because
	 * update_sd_pick_busiest() reads this before assignment.
	 */
	*sds = (struct sd_lb_stats){
		.busiest = NULL,
		.local = NULL,
		.total_load = 0UL,
		.total_capacity = 0UL,
		.busiest_stat = {
			.avg_load = 0UL,
			.sum_nr_running = 0,
			.group_type = group_other,
		},
	};
}

/**
 * get_sd_load_idx - Obtain the load index for a given sched domain.
 * @sd: The sched_domain whose load_idx is to be obtained.
 * @idle: The idle status of the CPU for whose sd load_idx is obtained.
 *
 * Return: The load index.
 */
/*********************************************************************************************************
** 函数名称: get_sd_load_idx
** 功能描述: 获取指定调度域的指定 cpu_idle 类型的 cpu 负载统计数组索引值
** 输	 入: sd - 指定的调度域指针
**         : idle - 指定的 cpu_idle 类型
** 输	 出: load_idx - load_idx 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int get_sd_load_idx(struct sched_domain *sd,
					enum cpu_idle_type idle)
{
	int load_idx;

	switch (idle) {
	case CPU_NOT_IDLE:
		load_idx = sd->busy_idx;
		break;

	case CPU_NEWLY_IDLE:
		load_idx = sd->newidle_idx;
		break;
	default:
		load_idx = sd->idle_idx;
		break;
	}

	return load_idx;
}

/*********************************************************************************************************
** 函数名称: default_scale_capacity
** 功能描述: 获取指定的调度域上指定的 cpu 的默认频率负载计算能力缩放比
** 注     释: 获取到的默认频率负载计算能力是以 SCHED_CAPACITY_SHIFT 为基准分母的，详情见 update_cpu_capacity
** 输	 入: sd - 指定的调度域指针
**         : cpu - 指定的 cpu id
** 输	 出: unsigned long - 默认频率负载计算能力缩放比
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long default_scale_capacity(struct sched_domain *sd, int cpu)
{
	return SCHED_CAPACITY_SCALE;
}

/*********************************************************************************************************
** 函数名称: default_scale_capacity
** 功能描述: 获取指定的调度域上指定的 cpu 的频率负载计算能力缩放比
** 注     释: 1. 获取到的负载计算能力是以 SCHED_CAPACITY_SHIFT 为基准分母的，详情见 update_cpu_capacity
**         : 2. 这是个虚函数，可以在 arch 里面重新实现，如果 arch 没实现，则使用这个函数
** 输	 入: sd - 指定的调度域指针
**         : cpu - 指定的 cpu id
** 输	 出: unsigned long - 频率负载计算能力缩放比
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
unsigned long __weak arch_scale_freq_capacity(struct sched_domain *sd, int cpu)
{
	return default_scale_capacity(sd, cpu);
}

/*********************************************************************************************************
** 函数名称: default_scale_capacity
** 功能描述: 获取指定的调度域上指定的 cpu 默认 cpu 负载计算能力
** 注     释: 获取到的负载计算能力是以 SCHED_CAPACITY_SHIFT 为基准分母的，详情见 update_cpu_capacity
** 输	 入: sd - 指定的调度域指针
**         : cpu - 指定的 cpu id
** 输	 出: unsigned long - 负载计算能力，单位是 DMIPS / MHZ
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long default_scale_cpu_capacity(struct sched_domain *sd, int cpu)
{
	if ((sd->flags & SD_SHARE_CPUCAPACITY) && (sd->span_weight > 1))
		return sd->smt_gain / sd->span_weight;

	return SCHED_CAPACITY_SCALE;
}

/*********************************************************************************************************
** 函数名称: arch_scale_cpu_capacity
** 功能描述: 获取指定的调度域上指定的 cpu 的负载计算能力
** 注     释: 1. 获取到的负载计算能力是以 SCHED_CAPACITY_SHIFT 为基准分母的，详情见 update_cpu_capacity
**         : 2. 这是个虚函数，可以在 arch 里面重新实现，如果 arch 没实现，则使用这个函数
** 输	 入: sd - 指定的调度域指针
**         : cpu - 指定的 cpu id
** 输	 出: unsigned long - 负载计算能力，单位是 DMIPS / MHZ
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
unsigned long __weak arch_scale_cpu_capacity(struct sched_domain *sd, int cpu)
{
	return default_scale_cpu_capacity(sd, cpu);
}

/*********************************************************************************************************
** 函数名称: scale_rt_capacity
** 功能描述: 获取指定的 cpu 上把实时调度实例运行时间去除之后为 cfs 调度实例剩余的负载计算能力所占比例
** 注     释: 获取到的负载计算能力扩大了 SCHED_CAPACITY_SCALE 倍，详情见 update_cpu_capacity
** 输	 入: cpu - 指定的 cpu id
** 输	 出: unsigned long - 剩余的负载计算能力所占比例，扩大了 SCHED_CAPACITY_SCALE 倍
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned long scale_rt_capacity(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	u64 total, available, age_stamp, avg;
	s64 delta;

	/*
	 * Since we're reading these variables without serialization make sure
	 * we read them once before doing sanity checks on them.
	 */
	age_stamp = ACCESS_ONCE(rq->age_stamp);
	avg = ACCESS_ONCE(rq->rt_avg);
	delta = __rq_clock_broken(rq) - age_stamp;

	if (unlikely(delta < 0))
		delta = 0;

	total = sched_avg_period() + delta;

	if (unlikely(total < avg)) {
		/* Ensures that capacity won't end up being negative */
		available = 0;
	} else {
		available = total - avg;
	}

	if (unlikely((s64)total < SCHED_CAPACITY_SCALE))
		total = SCHED_CAPACITY_SCALE;

	total >>= SCHED_CAPACITY_SHIFT;

    /* avg   = rq->rt_avg
    
       total = sched_avg_period() + delta

       delta = __rq_clock_broken(rq) - age_stamp
             = rq->clock - rq->age_stamp
             
       available = total - avg 
                 = sched_avg_period() + delta - avg 
                 = sched_avg_period() + delta - rq->rt_avg 
                 
       return = available / (total >> SCHED_CAPACITY_SHIFT)
       
              = (sched_avg_period() + delta - rq->rt_avg) / ((sched_avg_period() + delta) >> SCHED_CAPACITY_SHIFT)
              
                     sched_avg_period() + delta - rq->rt_avg
              = ----------------------------------------------------
                (sched_avg_period() + delta) >> SCHED_CAPACITY_SHIFT
                                           
                      0.5S + delta - rq->rt_avg
              = --------------------------------------
                (0.5S + delta) >> SCHED_CAPACITY_SHIFT

                0.5S + rq->clock - rq->age_stamp - rq->rt_avg
              = --------------------------------------------- << SCHED_CAPACITY_SHIFT
                      0.5S + rq->clock - rq->age_stamp     */
	return div_u64(available, total);
}

/*********************************************************************************************************
** 函数名称: update_cpu_capacity
** 功能描述: 更新指定的调度域上指定 cpu 的负载计算能力信息，这些负载计算能力是以 SCHED_CAPACITY_SCALE
**         : 为基准归一化后的数值
** 输	 入: sd - 指定的调度域指针
**         : cpu - 指定的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void update_cpu_capacity(struct sched_domain *sd, int cpu)
{
	unsigned long capacity = SCHED_CAPACITY_SCALE;
	struct sched_group *sdg = sd->groups;

    /* 计算指定 cpu 在全速运行状态下的 cpu capacity */
	if (sched_feat(ARCH_CAPACITY))
		capacity *= arch_scale_cpu_capacity(sd, cpu);
	else
		capacity *= default_scale_cpu_capacity(sd, cpu);

	capacity >>= SCHED_CAPACITY_SHIFT;
	sdg->sgc->capacity_orig = capacity;

    /* 计算指定 cpu 在指定频率运行状态下的 cpu capacity */
	if (sched_feat(ARCH_CAPACITY))
		capacity *= arch_scale_freq_capacity(sd, cpu);
	else
		capacity *= default_scale_capacity(sd, cpu);

	capacity >>= SCHED_CAPACITY_SHIFT;

    /* 计算指定 cpu 在指定频率运行状态下移除 rt 调度类后为 cfs 调度类剩余的 cpu capacity */
	capacity *= scale_rt_capacity(cpu);
	capacity >>= SCHED_CAPACITY_SHIFT;

	if (!capacity)
		capacity = 1;

    /* 因为在调度域最下层的调度组中只有一个 cpu，所以我们把 cpu 的 capacity 存储在了他的调度组结构中 */
	cpu_rq(cpu)->cpu_capacity = capacity;
	sdg->sgc->capacity = capacity;
}

/*********************************************************************************************************
** 函数名称: update_group_capacity
** 功能描述: 更新指定的调度域上指定 cpu 所在调度组的负载计算能力信息，这些负载计算能力是以
**         : SCHED_CAPACITY_SCALE 为基准归一化后的数值
** 输	 入: sd - 指定的调度域指针
**         : cpu - 指定的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void update_group_capacity(struct sched_domain *sd, int cpu)
{
	struct sched_domain *child = sd->child;
	struct sched_group *group, *sdg = sd->groups;
	unsigned long capacity, capacity_orig;
	unsigned long interval;

	interval = msecs_to_jiffies(sd->balance_interval);
	interval = clamp(interval, 1UL, max_load_balance_interval);
	sdg->sgc->next_update = jiffies + interval;

    /* 如果已经到了调度域最底层，即一个调度组内只有一个 cpu，则直接更新这个 cpu 的 capacity 并返回 */
	if (!child) {
		update_cpu_capacity(sd, cpu);
		return;
	}

	capacity_orig = capacity = 0;

	if (child->flags & SD_OVERLAP) {
		/*
		 * SD_OVERLAP domains cannot assume that child groups
		 * span the current group.
		 */
        /* 如果子调度组之间存在交叉 cpu，则遍历指定调度组内每一个 cpu 并统计他们的负载计算能力 */
		for_each_cpu(cpu, sched_group_cpus(sdg)) {
			struct sched_group_capacity *sgc;
			struct rq *rq = cpu_rq(cpu);

			/*
			 * build_sched_domains() -> init_sched_groups_capacity()
			 * gets here before we've attached the domains to the
			 * runqueues.
			 *
			 * Use capacity_of(), which is set irrespective of domains
			 * in update_cpu_capacity().
			 *
			 * This avoids capacity/capacity_orig from being 0 and
			 * causing divide-by-zero issues on boot.
			 *
			 * Runtime updates will correct capacity_orig.
			 */
			if (unlikely(!rq->sd)) {
				capacity_orig += capacity_of(cpu);
				capacity += capacity_of(cpu);
				continue;
			}

			sgc = rq->sd->groups->sgc;
			capacity_orig += sgc->capacity_orig;
			capacity += sgc->capacity;
		}
	} else  {
		/*
		 * !SD_OVERLAP domains can assume that child groups
		 * span the current group.
		 */ 
		/* 如果子调度组之间没有交叉 cpu，则统计指定调度域子节点的所有调度组的负载能力 */
		group = child->groups;
		do {
			capacity_orig += group->sgc->capacity_orig;
			capacity += group->sgc->capacity;
			group = group->next;
		} while (group != child->groups);
	}

	sdg->sgc->capacity_orig = capacity_orig;
	sdg->sgc->capacity = capacity;
}

/*
 * Try and fix up capacity for tiny siblings, this is needed when
 * things like SD_ASYM_PACKING need f_b_g to select another sibling
 * which on its own isn't powerful enough.
 *
 * See update_sd_pick_busiest() and check_asym_packing().
 */
static inline int
fix_small_capacity(struct sched_domain *sd, struct sched_group *group)
{
	/*
	 * Only siblings can have significantly less than SCHED_CAPACITY_SCALE
	 */
	if (!(sd->flags & SD_SHARE_CPUCAPACITY))
		return 0;

	/*
	 * If ~90% of the cpu_capacity is still there, we're good.
	 */
	if (group->sgc->capacity * 32 > group->sgc->capacity_orig * 29)
		return 1;

	return 0;
}

/*
 * Group imbalance indicates (and tries to solve) the problem where balancing
 * groups is inadequate due to tsk_cpus_allowed() constraints.
 *
 * Imagine a situation of two groups of 4 cpus each and 4 tasks each with a
 * cpumask covering 1 cpu of the first group and 3 cpus of the second group.
 * Something like:
 *
 * 	{ 0 1 2 3 } { 4 5 6 7 }
 * 	        *     * * *
 *
 * If we were to balance group-wise we'd place two tasks in the first group and
 * two tasks in the second group. Clearly this is undesired as it will overload
 * cpu 3 and leave one of the cpus in the second group unused.
 *
 * The current solution to this issue is detecting the skew in the first group
 * by noticing the lower domain failed to reach balance and had difficulty
 * moving tasks due to affinity constraints.
 *
 * When this is so detected; this group becomes a candidate for busiest; see
 * update_sd_pick_busiest(). And calculate_imbalance() and
 * find_busiest_group() avoid some of the usual balance conditions to allow it
 * to create an effective group imbalance.
 *
 * This is a somewhat tricky proposition since the next run might not find the
 * group imbalance and decide the groups need to be balanced again. A most
 * subtle and fragile situation.
 */
/*********************************************************************************************************
** 函数名称: sg_imbalanced
** 功能描述: 判断指定调度组是否因为 cpu 亲和力导致不能进行负载均衡任务迁移操作
** 输	 入: group - 指定的调度组指针
** 输	 出: int - 获取到的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int sg_imbalanced(struct sched_group *group)
{
	return group->sgc->imbalance;
}

/*
 * Compute the group capacity factor.
 *
 * Avoid the issue where N*frac(smt_capacity) >= 1 creates 'phantom' cores by
 * first dividing out the smt factor and computing the actual number of cores
 * and limit unit capacity with that.
 */
/*********************************************************************************************************
** 函数名称: sg_capacity_factor
** 功能描述: 根据指定的负载均衡环境数据计算指定的调度组的负载计算能力可以运行的调度任务个数
** 输	 入: env - 定的负载均衡环境数据指针
**         : group - 指定的调度组指针
** 输	 出: capacity_factor - 可以运行的调度任务个数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int sg_capacity_factor(struct lb_env *env, struct sched_group *group)
{
	unsigned int capacity_factor, smt, cpus;
	unsigned int capacity, capacity_orig;

	capacity = group->sgc->capacity;
	capacity_orig = group->sgc->capacity_orig;
	cpus = group->group_weight;

	/* smt := ceil(SCHED_CAPACITY_SCALE * cpus / capacity_orig)，assumes: 1 < smt_capacity < 2

	   因为 capacity_orig 是当前调度组内所有 cpu 负载计算能力在归一化后的总和，所以
	   
		  capacity_orig
	   -------------------- 就是当前调度组一共可以运行的调度任务个数，所以
	   SCHED_CAPACITY_SCALE
	   
                       capacity_orig
	   1 / smt = --------------------------- 就是当前调度组内每个 cpu 平均可以运行的调度任务个数
	             SCHED_CAPACITY_SCALE * cpus */
	smt = DIV_ROUND_UP(SCHED_CAPACITY_SCALE * cpus, capacity_orig);

	/* 因为 1 / smt 是当前调度组内每个 cpu 平均可以运行的调度任务个数，所以
	   capacity_factor = cpus * (1 / smt) 就是当前调度组一共可以运行的调度任务个数
	   capacity_factor 的值是经过向上取整计算后得出的 */
	capacity_factor = cpus / smt; /* cores */

    /* 从向上取整计算结果和四舍五入计算结果中取较小值作为最终结果 */
	capacity_factor = min_t(unsigned,
		capacity_factor, DIV_ROUND_CLOSEST(capacity, SCHED_CAPACITY_SCALE));
	
	if (!capacity_factor)
		capacity_factor = fix_small_capacity(env->sd, group);

	return capacity_factor;
}

/*********************************************************************************************************
** 函数名称: group_classify
** 功能描述: 根据指定的调度组负载均衡统计数据获取指定调度组的负载状态类别信息
** 输	 入: group - 指定的调度组指针
**         : sgs - 指定的调度组负载均衡统计数据结构指针
** 输	 出: group_type - 调度组的负载状态类别信息
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static enum group_type
group_classify(struct sched_group *group, struct sg_lb_stats *sgs)
{
	if (sgs->sum_nr_running > sgs->group_capacity_factor)
		return group_overloaded;

	if (sg_imbalanced(group))
		return group_imbalanced;

	return group_other;
}

/**
 * update_sg_lb_stats - Update sched_group's statistics for load balancing.
 * @env: The load balancing environment.
 * @group: sched_group whose statistics are to be updated.
 * @load_idx: Load index of sched_domain of this_cpu for load calc.
 * @local_group: Does group contain this_cpu.
 * @sgs: variable to hold the statistics for this group.
 * @overload: Indicate more than one runnable task for any CPU.
 */
/*********************************************************************************************************
** 函数名称: update_sg_lb_stats
** 功能描述: 根据指定的函数参数更新指定的调度组统计信息并存储到指定的调度组统计信息数据结构中
** 输	 入: env - 指定的负载均衡环境指针
**         : group - 指定的负载均衡任务迁移目的调度组指针
**         : load_idx - 指定的 load_idx 值
**         : local_group - 表示当前正在运行的 cpu 是否包含在指定的调度组中
** 输	 出: sgs - 指定的调度组统计信息数据结构指针
**         : overload - 表示指定的调度组内匹配的目的 cpu 运行队列上是否存在超过了一个调度实例的 cpu
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_sg_lb_stats(struct lb_env *env,
			struct sched_group *group, int load_idx,
			int local_group, struct sg_lb_stats *sgs,
			bool *overload)
{
	unsigned long load;
	int i;

	memset(sgs, 0, sizeof(*sgs));

    /* 遍历指定的目的调度组内和负载均衡环境指定的迁移目的匹配的每一个 cpu */
	for_each_cpu_and(i, sched_group_cpus(group), env->cpus) {
		struct rq *rq = cpu_rq(i);

		/* Bias balancing toward cpus of our domain */
		if (local_group)
			load = target_load(i, load_idx);
		else
			load = source_load(i, load_idx);

		sgs->group_load += load;
		sgs->sum_nr_running += rq->cfs.h_nr_running;

        /* 只要有一个 cpu 运行队列上的调度实例个数大于一就设置 *overload = true; */
		if (rq->nr_running > 1)
			*overload = true;

#ifdef CONFIG_NUMA_BALANCING
		sgs->nr_numa_running += rq->nr_numa_running;
		sgs->nr_preferred_running += rq->nr_preferred_running;
#endif
		sgs->sum_weighted_load += weighted_cpuload(i);
		if (idle_cpu(i))
			sgs->idle_cpus++;
	}

	/* Adjust by relative CPU capacity of the group */
	sgs->group_capacity = group->sgc->capacity;

	/* 计算当前调度组的平均负载贡献值占当前调度组的负载计算能力的百分比（乘以了精度因子）*/
	sgs->avg_load = (sgs->group_load*SCHED_CAPACITY_SCALE) / sgs->group_capacity;

	if (sgs->sum_nr_running)
		sgs->load_per_task = sgs->sum_weighted_load / sgs->sum_nr_running;

	sgs->group_weight = group->group_weight;
	sgs->group_capacity_factor = sg_capacity_factor(env, group);
	sgs->group_type = group_classify(group, sgs);

	if (sgs->group_capacity_factor > sgs->sum_nr_running)
		sgs->group_has_free_capacity = 1;
}

/**
 * update_sd_pick_busiest - return 1 on busiest group
 * @env: The load balancing environment.
 * @sds: sched_domain statistics
 * @sg: sched_group candidate to be checked for being the busiest
 * @sgs: sched_group statistics
 *
 * Determine if @sg is a busier group than the previously selected
 * busiest group.
 *
 * Return: %true if @sg is a busier group than the previously selected
 * busiest group. %false otherwise.
 */
/*********************************************************************************************************
** 函数名称: update_sd_pick_busiest
** 功能描述: 判断指定的调度组是否在指定的调度域内最忙，即平均负载最高
** 输	 入: env - 指定的负载均衡环境指针
**         : sds - 指定的调度域负载均衡状态统计结构指针
**         : sg - 指定的调度组指针
**         : sgs - 指定的调度组负载均衡状态统计结构指针
** 输	 出: true - 指定的调度组是最忙的
**         : false - 指定的调度组不是最忙的
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static bool update_sd_pick_busiest(struct lb_env *env,
				   struct sd_lb_stats *sds,
				   struct sched_group *sg,
				   struct sg_lb_stats *sgs)
{
	struct sg_lb_stats *busiest = &sds->busiest_stat;

	if (sgs->group_type > busiest->group_type)
		return true;

	if (sgs->group_type < busiest->group_type)
		return false;

	if (sgs->avg_load <= busiest->avg_load)
		return false;

	/* This is the busiest node in its class. */
	if (!(env->sd->flags & SD_ASYM_PACKING))
		return true;

	/*
	 * ASYM_PACKING needs to move all the work to the lowest
	 * numbered CPUs in the group, therefore mark all groups
	 * higher than ourself as busy.
	 */
	if (sgs->sum_nr_running && env->dst_cpu < group_first_cpu(sg)) {
		if (!sds->busiest)
			return true;

		if (group_first_cpu(sds->busiest) > group_first_cpu(sg))
			return true;
	}

	return false;
}

#ifdef CONFIG_NUMA_BALANCING
/*********************************************************************************************************
** 函数名称: fbq_classify_group
** 功能描述: 根据指定的调度组状态获取它的 fbq 类型
** 输	 入: sgs - 指定的调度组负载均衡状态统计数据结构指针
** 输	 出: fbq_type - 获取到的 fbq 类型
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline enum fbq_type fbq_classify_group(struct sg_lb_stats *sgs)
{
	if (sgs->sum_nr_running > sgs->nr_numa_running)
		return regular;
	if (sgs->sum_nr_running > sgs->nr_preferred_running)
		return remote;
	return all;
}

/*********************************************************************************************************
** 函数名称: fbq_classify_rq
** 功能描述: 根据指定的 cpu 运行队列状态获取它的 fbq 类型
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: fbq_type - 获取到的 fbq 类型
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline enum fbq_type fbq_classify_rq(struct rq *rq)
{
	if (rq->nr_running > rq->nr_numa_running)
		return regular;
	if (rq->nr_running > rq->nr_preferred_running)
		return remote;
	return all;
}
#else
/*********************************************************************************************************
** 函数名称: fbq_classify_group
** 功能描述: 根据指定的调度组状态获取它的 fbq 类型
** 输	 入: sgs - 指定的调度组负载均衡状态统计数据结构指针
** 输	 出: fbq_type - 获取到的 fbq 类型
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline enum fbq_type fbq_classify_group(struct sg_lb_stats *sgs)
{
	return all;
}

/*********************************************************************************************************
** 函数名称: fbq_classify_rq
** 功能描述: 根据指定的 cpu 运行队列状态获取它的 fbq 类型
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: fbq_type - 获取到的 fbq 类型
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline enum fbq_type fbq_classify_rq(struct rq *rq)
{
	return regular;
}
#endif /* CONFIG_NUMA_BALANCING */

/**
 * update_sd_lb_stats - Update sched_domain's statistics for load balancing.
 * @env: The load balancing environment.
 * @sds: variable to hold the statistics for this sched_domain.
 */
/*********************************************************************************************************
** 函数名称: update_sd_lb_stats
** 功能描述: 根据指定的负载均衡环境更新指定的调度域的负载能力和计算能力信息并找出本地调度组和最忙调度组
** 输	 入: env - 指定的负载均衡环境指针
** 输	 出: sds - 指定的调度域负载均衡状态统计结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void update_sd_lb_stats(struct lb_env *env, struct sd_lb_stats *sds)
{
	struct sched_domain *child = env->sd->child;
	struct sched_group *sg = env->sd->groups;
	struct sg_lb_stats tmp_sgs;
	int load_idx, prefer_sibling = 0;
	bool overload = false;

	if (child && child->flags & SD_PREFER_SIBLING)
		prefer_sibling = 1;

	load_idx = get_sd_load_idx(env->sd, env->idle);

    /* 遍历指定的调度域内的所有调度组，更新调度域的负载能力和计算能力信息并找出最忙的调度组 */
	do {
		struct sg_lb_stats *sgs = &tmp_sgs;
		int local_group;

		local_group = cpumask_test_cpu(env->dst_cpu, sched_group_cpus(sg));
		if (local_group) {
			sds->local = sg;
			sgs = &sds->local_stat;

			if (env->idle != CPU_NEWLY_IDLE ||
			    time_after_eq(jiffies, sg->sgc->next_update))
				update_group_capacity(env->sd, env->dst_cpu);
		}

        /* 更新当前调度组的负载均衡统计数据信息 */
		update_sg_lb_stats(env, sg, load_idx, local_group, sgs,
						&overload);

		if (local_group)
			goto next_group;

		/*
		 * In case the child domain prefers tasks go to siblings
		 * first, lower the sg capacity factor to one so that we'll try
		 * and move all the excess tasks away. We lower the capacity
		 * of a group only if the local group has the capacity to fit
		 * these excess tasks, i.e. nr_running < group_capacity_factor. The
		 * extra check prevents the case where you always pull from the
		 * heaviest group when it is already under-utilized (possible
		 * with a large weight task outweighs the tasks on the system).
		 */
		if (prefer_sibling && sds->local &&
		    sds->local_stat.group_has_free_capacity) {
			sgs->group_capacity_factor = min(sgs->group_capacity_factor, 1U);
			sgs->group_type = group_classify(sg, sgs);
		}

        /* 找出最忙的调度组 */
		if (update_sd_pick_busiest(env, sds, sg, sgs)) {
			sds->busiest = sg;
			sds->busiest_stat = *sgs;
		}

next_group:
		/* Now, start updating sd_lb_stats */
		sds->total_load += sgs->group_load;
		sds->total_capacity += sgs->group_capacity;

		sg = sg->next;
	} while (sg != env->sd->groups);

	if (env->sd->flags & SD_NUMA)
		env->fbq_type = fbq_classify_group(&sds->busiest_stat);

    /* 如果当前负载均衡环境指定的调度域是调度域属性结构的根节点，则更新根调度域的过载标识变量值 */
	if (!env->sd->parent) {
		/* update overload indicator if we are at root domain */
		if (env->dst_rq->rd->overload != overload)
			env->dst_rq->rd->overload = overload;
	}

}

/**
 * check_asym_packing - Check to see if the group is packed into the
 *			sched doman.
 *
 * This is primarily intended to used at the sibling level.  Some
 * cores like POWER7 prefer to use lower numbered SMT threads.  In the
 * case of POWER7, it can move to lower SMT modes only when higher
 * threads are idle.  When in lower SMT modes, the threads will
 * perform better since they share less core resources.  Hence when we
 * have idle threads, we want them to be the higher ones.
 *
 * This packing function is run on idle threads.  It checks to see if
 * the busiest CPU in this domain (core in the P7 case) has a higher
 * CPU number than the packing function is being run on.  Here we are
 * assuming lower CPU number will be equivalent to lower a SMT thread
 * number.
 *
 * Return: 1 when packing is required and a task should be moved to
 * this CPU.  The amount of the imbalance is returned in *imbalance.
 *
 * @env: The load balancing environment.
 * @sds: Statistics of the sched_domain which is to be packed
 */
/* SD_ASYM_PACKING 原则是什么：
   因为在类似 POWER7 核心中，在 cpu id 较低的 cpu 上运行 SMT 将会有更好的性能
   （因为他们共享更少的核心运算单元），所以我们要尽量把处于运行状态的 SMT 线程
   迁移到 cpu id 较小的 cpu 核上，而把处于 idle 状态的 SMT 线程迁移到 cpu id
   较大的 cpu 核上，这样会是系统性能更高（这个函数运行在 idle cpu 上）*/
/*********************************************************************************************************
** 函数名称: check_asym_packing
** 功能描述: 根据指定的函数参数检查是否可以根据 SD_ASYM_PACKING 原则向当前 cpu 上迁移任务
** 输	 入: env - 指定的负载均衡环境指针
**         : sds - 指定的调度域负载均衡状态统计结构指针
** 输	 出: env->imbalance - 当前负载均衡环境一共需要平衡的负载量（即已经失衡的负载量）
**         : 1 - 可以向当前 cpu 迁移任务
**         : 0 - 不可以向当前 cpu 迁移任务
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int check_asym_packing(struct lb_env *env, struct sd_lb_stats *sds)
{
	int busiest_cpu;

	if (!(env->sd->flags & SD_ASYM_PACKING))
		return 0;

	if (!sds->busiest)
		return 0;

    /* busiest_cpu 表示的是负载较高的 cpu id，而 env->dst_cpu 表示的是负载较低的 cpu id 
	   这个地方在判断负载较低的 cpu 的 id 值是否要比负载较高的 cpu 的 id 值大，如果负载
	   低的 cpu id 值比较大，表示和 SD_ASYM_PACKING 原则相违背，所以我们不能进行任务迁移 */
	busiest_cpu = group_first_cpu(sds->busiest);
	if (env->dst_cpu > busiest_cpu)
		return 0;

	env->imbalance = DIV_ROUND_CLOSEST(
		sds->busiest_stat.avg_load * sds->busiest_stat.group_capacity,
		SCHED_CAPACITY_SCALE);

	return 1;
}

/**
 * fix_small_imbalance - Calculate the minor imbalance that exists
 *			amongst the groups of a sched_domain, during
 *			load balancing.
 * @env: The load balancing environment.
 * @sds: Statistics of the sched_domain whose imbalance is to be calculated.
 */
/*********************************************************************************************************
** 函数名称: fix_small_imbalance
** 功能描述: 在计算指定调度域内调度组间负载失衡时，如果这些调度组之间的负载失衡较小，不足以迁移一个任务
**         : 的时候，会通过这个函数来处理这种微小不平衡，尝试将负载失衡量向上取整到一个任务的负载量，这
**         : 样就可以进行任务迁移操作了
** 输	 入: env - 指定的负载均衡环境指针
**         : sds - 指定的调度域负载均衡状态统计结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline
void fix_small_imbalance(struct lb_env *env, struct sd_lb_stats *sds)
{
	unsigned long tmp, capa_now = 0, capa_move = 0;
	unsigned int imbn = 2;
	unsigned long scaled_busy_load_per_task;
	struct sg_lb_stats *local, *busiest;

	local = &sds->local_stat;
	busiest = &sds->busiest_stat;

	if (!local->sum_nr_running)
		local->load_per_task = cpu_avg_load_per_task(env->dst_cpu);
	else if (busiest->load_per_task > local->load_per_task)
		imbn = 1;

    /* 计算指定调度域中最忙调度组中每个任务在过去一段时间内经过衰减“加权”的
       负载贡献占这个调度组的负载计算能力的百分比（乘以了精度因子）*/
	scaled_busy_load_per_task =
		(busiest->load_per_task * SCHED_CAPACITY_SCALE) /
		busiest->group_capacity;

    /* 为了能够执行负载失衡任务迁移操作，如果指定调度域内最忙调度组比当前调度组的平均
	   负载贡献值大指定阈值时，将失衡负载量向上取整到一个 busiest->load_per_task */
	if (busiest->avg_load + scaled_busy_load_per_task >=
	    local->avg_load + (scaled_busy_load_per_task * imbn)) {
		env->imbalance = busiest->load_per_task;
		return;
	}

	/*
	 * OK, we don't have enough imbalance to justify moving tasks,
	 * however we may be able to increase total CPU capacity used by
	 * moving them.
	 */
	capa_now += busiest->group_capacity *
			min(busiest->load_per_task, busiest->avg_load);
	capa_now += local->group_capacity *
			min(local->load_per_task, local->avg_load);
	capa_now /= SCHED_CAPACITY_SCALE;

	/* Amount of load we'd subtract */
	if (busiest->avg_load > scaled_busy_load_per_task) {
		capa_move += busiest->group_capacity *
			    min(busiest->load_per_task,
				busiest->avg_load - scaled_busy_load_per_task);
	}

	/* Amount of load we'd add */
	if (busiest->avg_load * busiest->group_capacity <
	    busiest->load_per_task * SCHED_CAPACITY_SCALE) {
		tmp = (busiest->avg_load * busiest->group_capacity) /
		      local->group_capacity;
	} else {
		tmp = (busiest->load_per_task * SCHED_CAPACITY_SCALE) /
		      local->group_capacity;
	}
	capa_move += local->group_capacity *
		    min(local->load_per_task, local->avg_load + tmp);
	capa_move /= SCHED_CAPACITY_SCALE;

	/* Move if we gain throughput */
	if (capa_move > capa_now)
		env->imbalance = busiest->load_per_task;
}

/**
 * calculate_imbalance - Calculate the amount of imbalance present within the
 *			 groups of a given sched_domain during load balance.
 * @env: load balance environment
 * @sds: statistics of the sched_domain whose imbalance is to be calculated.
 */
/*********************************************************************************************************
** 函数名称: calculate_imbalance
** 功能描述: 根据函数参数计算指定调度域内最忙调度组的最小负载失衡量
** 输	 入: env - 指定的负载均衡环境指针
**         : sds - 指定的调度域负载均衡状态统计结构指针
** 输	 出: env->imbalance - 指定的调度域内最忙调度组的最小负载失衡量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void calculate_imbalance(struct lb_env *env, struct sd_lb_stats *sds)
{
	unsigned long max_pull, load_above_capacity = ~0UL;
	struct sg_lb_stats *local, *busiest;

	local = &sds->local_stat;
	busiest = &sds->busiest_stat;

	if (busiest->group_type == group_imbalanced) {
		/*
		 * In the group_imb case we cannot rely on group-wide averages
		 * to ensure cpu-load equilibrium, look at wider averages. XXX
		 */
		busiest->load_per_task =
			min(busiest->load_per_task, sds->avg_load);
	}

	/*
	 * In the presence of smp nice balancing, certain scenarios can have
	 * max load less than avg load(as we skip the groups at or below
	 * its cpu_capacity, while calculating max_load..)
	 */
	if (busiest->avg_load <= sds->avg_load ||
	    local->avg_load >= sds->avg_load) {
		env->imbalance = 0;
		return fix_small_imbalance(env, sds);
	}

	/*
	 * If there aren't any idle cpus, avoid creating some.
	 */
	if (busiest->group_type == group_overloaded &&
	    local->group_type   == group_overloaded) {
		load_above_capacity =
			(busiest->sum_nr_running - busiest->group_capacity_factor);

        /* 表示的是过载任务贡献的负载量（乘以了精度因子）*/
		load_above_capacity *= (SCHED_LOAD_SCALE * SCHED_CAPACITY_SCALE);

		/* 表示的是过载任务贡献的负载量占当前调度组的负载计算能力的百分比（乘以了精度因子）*/
		load_above_capacity /= busiest->group_capacity;
	}

	/*
	 * We're trying to get all the cpus to the average_load, so we don't
	 * want to push ourselves above the average load, nor do we wish to
	 * reduce the max loaded cpu below the average load. At the same time,
	 * we also don't want to reduce the group load below the group capacity
	 * (so that we can implement power-savings policies etc). Thus we look
	 * for the minimum possible imbalance.
	 */
	/* 我们尝试将所有的 cpu 负载都平衡到当前调度域的平均负载，因此我们不想让
	   当前 cpu 的负载超过当前调度域的平均负载，也不希望将 cpu 的最大负载降低
	   到低于当前调度域的平均负载。同时，我们也不希望将实际运行负载降低到低于
	   组负载计算能力(以便我们可以实现省电策略等)。因此，我们需要寻找可能的最
	   小不平衡 */

    /* 计算指定调度域内最忙调度组的最小负载失衡量百分比（乘以了精度因子）*/
	max_pull = min(busiest->avg_load - sds->avg_load, load_above_capacity);

	/* How much load to actually move to equalise the imbalance */
    /* 计算指定调度域内最忙调度组的最小负载失衡量 */
	env->imbalance = min(
		max_pull * busiest->group_capacity,
		(sds->avg_load - local->avg_load) * local->group_capacity
	) / SCHED_CAPACITY_SCALE;

	/*
	 * if *imbalance is less than the average load per runnable task
	 * there is no guarantee that any tasks will be moved so we'll have
	 * a think about bumping its value to force at least one task to be
	 * moved
	 */
	if (env->imbalance < busiest->load_per_task)
		return fix_small_imbalance(env, sds);
}

/******* find_busiest_group() helpers end here *********************/

/**
 * find_busiest_group - Returns the busiest group within the sched_domain
 * if there is an imbalance. If there isn't an imbalance, and
 * the user has opted for power-savings, it returns a group whose
 * CPUs can be put to idle by rebalancing those tasks elsewhere, if
 * such a group exists.
 *
 * Also calculates the amount of weighted load which should be moved
 * to restore balance.
 *
 * @env: The load balancing environment.
 *
 * Return:	- The busiest group if imbalance exists.
 *		- If no imbalance and user has opted for power-savings balance,
 *		   return the least loaded group whose CPUs can be
 *		   put to idle by rebalancing its tasks onto our group.
 */
/*********************************************************************************************************
** 函数名称: find_busiest_group
** 功能描述: 根据指定的负载均衡环境找出指定的调度域内最忙调度组并计算最忙调度组的最小负载失衡量
** 输	 入: env - 指定的负载均衡环境指针
** 输	 出: sds.busiest - 最忙调度组指针
**         : env->imbalance - 最忙调度组的最小负载失衡量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct sched_group *find_busiest_group(struct lb_env *env)
{
	struct sg_lb_stats *local, *busiest;
	struct sd_lb_stats sds;

	init_sd_lb_stats(&sds);

	/*
	 * Compute the various statistics relavent for load balancing at
	 * this level.
	 */
	update_sd_lb_stats(env, &sds);
	local = &sds.local_stat;
	busiest = &sds.busiest_stat;

	if ((env->idle == CPU_IDLE || env->idle == CPU_NEWLY_IDLE) &&
	    check_asym_packing(env, &sds))
		return sds.busiest;

	/* There is no busy sibling group to pull tasks from */
	if (!sds.busiest || busiest->sum_nr_running == 0)
		goto out_balanced;

	/* 计算当前调度域的平均负载贡献值占当前调度域的负载计算能力的百分比（乘以了精度因子）*/
	sds.avg_load = (SCHED_CAPACITY_SCALE * sds.total_load)
						/ sds.total_capacity;

	/*
	 * If the busiest group is imbalanced the below checks don't
	 * work because they assume all things are equal, which typically
	 * isn't true due to cpus_allowed constraints and the like.
	 */
	if (busiest->group_type == group_imbalanced)
		goto force_balance;

	/* SD_BALANCE_NEWIDLE trumps SMP nice when underutilized */
	if (env->idle == CPU_NEWLY_IDLE && local->group_has_free_capacity &&
	    !busiest->group_has_free_capacity)
		goto force_balance;

	/*
	 * If the local group is busier than the selected busiest group
	 * don't try and pull any tasks.
	 */
	if (local->avg_load >= busiest->avg_load)
		goto out_balanced;

	/*
	 * Don't pull any tasks if this group is already above the domain
	 * average load.
	 */
	if (local->avg_load >= sds.avg_load)
		goto out_balanced;

	if (env->idle == CPU_IDLE) {
		/*
		 * This cpu is idle. If the busiest group is not overloaded
		 * and there is no imbalance between this and busiest group
		 * wrt idle cpus, it is balanced. The imbalance becomes
		 * significant if the diff is greater than 1 otherwise we
		 * might end up to just move the imbalance on another group
		 */
		if ((busiest->group_type != group_overloaded) &&
				(local->idle_cpus <= (busiest->idle_cpus + 1)))
			goto out_balanced;
	} else {
		/*
		 * In the CPU_NEWLY_IDLE, CPU_NOT_IDLE cases, use
		 * imbalance_pct to be conservative.
		 */
		if (100 * busiest->avg_load <=
				env->sd->imbalance_pct * local->avg_load)
			goto out_balanced;
	}

force_balance:
	/* Looks like there is an imbalance. Compute it */
	calculate_imbalance(env, &sds);
	return sds.busiest;

out_balanced:
	env->imbalance = 0;
	return NULL;
}

/*
 * find_busiest_queue - find the busiest runqueue among the cpus in group.
 */
/*********************************************************************************************************
** 函数名称: find_busiest_queue
** 功能描述: 根据指定的负载均衡环境查找指定的调度组内最忙的 cpu 运行队列
** 输	 入: env - 指定的负载均衡环境指针
**         : group - 指定的最忙调度组指针
** 输	 出: busiest - 最忙 cpu 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static struct rq *find_busiest_queue(struct lb_env *env,
				     struct sched_group *group)
{
	struct rq *busiest = NULL, *rq;
	unsigned long busiest_load = 0, busiest_capacity = 1;
	int i;

    /* 遍历指定的最忙调度组内和负载均衡任务迁移目的 cpu 匹配的 cpu */
	for_each_cpu_and(i, sched_group_cpus(group), env->cpus) {
		unsigned long capacity, capacity_factor, wl;
		enum fbq_type rt;

		rq = cpu_rq(i);
		rt = fbq_classify_rq(rq);

		/*
		 * We classify groups/runqueues into three groups:
		 *  - regular: there are !numa tasks
		 *  - remote:  there are numa tasks that run on the 'wrong' node
		 *  - all:     there is no distinction
		 *
		 * In order to avoid migrating ideally placed numa tasks,
		 * ignore those when there's better options.
		 *
		 * If we ignore the actual busiest queue to migrate another
		 * task, the next balance pass can still reduce the busiest
		 * queue by moving tasks around inside the node.
		 *
		 * If we cannot move enough load due to this classification
		 * the next pass will adjust the group classification and
		 * allow migration of more tasks.
		 *
		 * Both cases only affect the total convergence complexity.
		 */
		if (rt > env->fbq_type)
			continue;

		capacity = capacity_of(i);
		capacity_factor = DIV_ROUND_CLOSEST(capacity, SCHED_CAPACITY_SCALE);
		if (!capacity_factor)
			capacity_factor = fix_small_capacity(env->sd, group);

		wl = weighted_cpuload(i);

		/*
		 * When comparing with imbalance, use weighted_cpuload()
		 * which is not scaled with the cpu capacity.
		 */
		/* env->imbalance = min(max_pull * busiest->group_capacity, 
		                       (sds->avg_load - local->avg_load) * local->group_capacity)
		                       / SCHED_CAPACITY_SCALE; */
		if (capacity_factor && rq->nr_running == 1 && wl > env->imbalance)
			continue;

		/*
		 * For the load comparisons with the other cpu's, consider
		 * the weighted_cpuload() scaled with the cpu capacity, so
		 * that the load can be moved away from the cpu that is
		 * potentially running at a lower capacity.
		 *
		 * Thus we're looking for max(wl_i / capacity_i), crosswise
		 * multiplication to rid ourselves of the division works out
		 * to: wl_i * capacity_j > wl_j * capacity_i;  where j is
		 * our previous maximum.
		 */
		/* 因为 wl * busiest_capacity > busiest_load * capacity，所以
		
              wl        busiest_load
           -------- > ----------------
           capacity   busiest_capacity   

           即我们选择平均加权衰减负载贡献值和 cpu 负载计算能力比值最大的 cpu 运行队列 */
		if (wl * busiest_capacity > busiest_load * capacity) {
			busiest_load = wl;
			busiest_capacity = capacity;
			busiest = rq;
		}
	}

	return busiest;
}

/*
 * Max backoff if we encounter pinned tasks. Pretty arbitrary value, but
 * so long as it is large enough.
 */
#define MAX_PINNED_INTERVAL	512

/* Working cpumask for load_balance and load_balance_newidle. */
/* 表示当前 cpu 负载均衡环境可以用来执行负载均衡的所有目的 cpu 候选者掩码值 */
DEFINE_PER_CPU(cpumask_var_t, load_balance_mask);

/*********************************************************************************************************
** 函数名称: need_active_balance
** 功能描述: 根据指定的负载均衡环境判断当前是否需要执行负载均衡操作
** 输	 入: env - 指定的负载均衡环境指针
** 输	 出: 1 - 需要执行负载均衡操作
**         : 0 - 不需要执行负载均衡操作
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int need_active_balance(struct lb_env *env)
{
	struct sched_domain *sd = env->sd;

	if (env->idle == CPU_NEWLY_IDLE) {

		/*
		 * ASYM_PACKING needs to force migrate tasks from busy but
		 * higher numbered CPUs in order to pack all tasks in the
		 * lowest numbered CPUs.
		 */
		if ((sd->flags & SD_ASYM_PACKING) && env->src_cpu > env->dst_cpu)
			return 1;
	}

	return unlikely(sd->nr_balance_failed > sd->cache_nice_tries+2);
}

static int active_load_balance_cpu_stop(void *data);

/*********************************************************************************************************
** 函数名称: need_active_balance
** 功能描述: 根据指定的负载均衡环境判断“是否可以”执行负载均衡操作
** 输	 入: env - 指定的负载均衡环境指针
** 输	 出: 1 - 可以执行负载均衡操作
**         : 0 - 不可以执行负载均衡操作
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int should_we_balance(struct lb_env *env)
{
	struct sched_group *sg = env->sd->groups;
	struct cpumask *sg_cpus, *sg_mask;
	int cpu, balance_cpu = -1;

	/*
	 * In the newly idle case, we will allow all the cpu's
	 * to do the newly idle load balance.
	 */
	if (env->idle == CPU_NEWLY_IDLE)
		return 1;

	sg_cpus = sched_group_cpus(sg);
	sg_mask = sched_group_mask(sg);
	
	/* Try to find first idle cpu */
	for_each_cpu_and(cpu, sg_cpus, env->cpus) {
		if (!cpumask_test_cpu(cpu, sg_mask) || !idle_cpu(cpu))
			continue;

		balance_cpu = cpu;
		break;
	}

	if (balance_cpu == -1)
		balance_cpu = group_balance_cpu(sg);

	/*
	 * First idle cpu or the first cpu(busiest) in this sched group
	 * is eligible for doing load balancing at this and above domains.
	 */
	return balance_cpu == env->dst_cpu;
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 */
/*********************************************************************************************************
** 函数名称: load_balance
** 功能描述: 尝试从指定的调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列上，使这两个 cpu
**         : 运行队列的负载处于平衡状态
** 输	 入: this_cpu - 指定的当前 cpu id
**         : this_rq - 指定的当前 cpu 运行队列指针
**         : sd - 指定的调度域指针
**         : idle - 指定的当前 cpu 的 idle 类型
** 输	 出: continue_balancing - 表示当前函数返回后是否需要继续执行负载均衡操作
**         : ld_moved - 成功迁移的任务数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int load_balance(int this_cpu, struct rq *this_rq,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *continue_balancing)
{
	int ld_moved, cur_ld_moved, active_balance = 0;
	struct sched_domain *sd_parent = sd->parent;
	struct sched_group *group;
	struct rq *busiest;
	unsigned long flags;
	struct cpumask *cpus = this_cpu_cpumask_var_ptr(load_balance_mask);

	struct lb_env env = {
		.sd		= sd,
		.dst_cpu	= this_cpu,
		.dst_rq		= this_rq,
		.dst_grpmask    = sched_group_cpus(sd->groups),
		.idle		= idle,
		.loop_break	= sched_nr_migrate_break,
		.cpus		= cpus,
		.fbq_type	= all,
		.tasks		= LIST_HEAD_INIT(env.tasks),
	};

	/*
	 * For NEWLY_IDLE load_balancing, we don't need to consider
	 * other cpus in our group
	 */
	if (idle == CPU_NEWLY_IDLE)
		env.dst_grpmask = NULL;

	cpumask_copy(cpus, cpu_active_mask);

	schedstat_inc(sd, lb_count[idle]);

redo:
	if (!should_we_balance(&env)) {
		*continue_balancing = 0;
		goto out_balanced;
	}

    /* 根据指定的负载均衡环境找出指定的调度域内最忙调度组并计算最忙调度组的最小负载失衡量 */
	group = find_busiest_group(&env);
	if (!group) {
		schedstat_inc(sd, lb_nobusyg[idle]);
		goto out_balanced;
	}

    /* 根据指定的负载均衡环境查找指定的调度组内最忙的 cpu 运行队列 */
	busiest = find_busiest_queue(&env, group);
	if (!busiest) {
		schedstat_inc(sd, lb_nobusyq[idle]);
		goto out_balanced;
	}

	BUG_ON(busiest == env.dst_rq);

	schedstat_add(sd, lb_imbalance[idle], env.imbalance);

	ld_moved = 0;
	if (busiest->nr_running > 1) {
		/*
		 * Attempt to move tasks. If find_busiest_group has found
		 * an imbalance but busiest->nr_running <= 1, the group is
		 * still unbalanced. ld_moved simply stays zero, so it is
		 * correctly treated as an imbalance.
		 */
		env.flags |= LBF_ALL_PINNED;
		env.src_cpu   = busiest->cpu;
		env.src_rq    = busiest;
		env.loop_max  = min(sysctl_sched_nr_migrate, busiest->nr_running);

more_balance:
		raw_spin_lock_irqsave(&busiest->lock, flags);

		/*
		 * cur_ld_moved - load moved in current iteration
		 * ld_moved     - cumulative load moved across iterations
		 */
		cur_ld_moved = detach_tasks(&env);

		/*
		 * We've detached some tasks from busiest_rq. Every
		 * task is masked "TASK_ON_RQ_MIGRATING", so we can safely
		 * unlock busiest->lock, and we are able to be sure
		 * that nobody can manipulate the tasks in parallel.
		 * See task_rq_lock() family for the details.
		 */

		raw_spin_unlock(&busiest->lock);

		if (cur_ld_moved) {
			attach_tasks(&env);
			ld_moved += cur_ld_moved;
		}

		local_irq_restore(flags);

		if (env.flags & LBF_NEED_BREAK) {
			env.flags &= ~LBF_NEED_BREAK;
			goto more_balance;
		}

		/*
		 * Revisit (affine) tasks on src_cpu that couldn't be moved to
		 * us and move them to an alternate dst_cpu in our sched_group
		 * where they can run. The upper limit on how many times we
		 * iterate on same src_cpu is dependent on number of cpus in our
		 * sched_group.
		 *
		 * This changes load balance semantics a bit on who can move
		 * load to a given_cpu. In addition to the given_cpu itself
		 * (or a ilb_cpu acting on its behalf where given_cpu is
		 * nohz-idle), we now have balance_cpu in a position to move
		 * load to given_cpu. In rare situations, this may cause
		 * conflicts (balance_cpu and given_cpu/ilb_cpu deciding
		 * _independently_ and at _same_ time to move some load to
		 * given_cpu) causing exceess load to be moved to given_cpu.
		 * This however should not happen so much in practice and
		 * moreover subsequent load balance cycles should correct the
		 * excess load moved.
		 */
		/* 重新访问源 cpu 上那些不可以迁移到当前 cpu 但是可以迁移到当前 cpu 所属调度组
		   的其他 cpu 上运行的任务，我们迭代访问源 cpu 的次数取决于当前 cpu 所属调度组
		   上包含的 cpu 个数

           (ilb - idle load balance)
           这在一定程度上改变了负载均衡的语义（即我们需要将负载迁移到指定的 cpu 上的语义）
           除了指定的 cpu 之外（或者在指定的 cpu 是 nohz-idle 状态时，让处于 ilb_cpu 代替
           它执行），我们现在还可以将 balance_cpu 的负载迁移到指定的 cpu 上面，在很少的情
           景下，这可能会出现冲突（即 balance_cpu 和指定的 cpu 或者 ilb_cpu 决定相互独立并
           在同一时间向指定的 cpu 上迁移负载），这将会导致迁移过多的负载到指定的 cpu 上。
           然而，在实际情况中这种情景是很少发生的，即使出现了这种情景，在随后的负载平衡周
           期中也会把过多迁移的负载平衡回来 */
		if ((env.flags & LBF_DST_PINNED) && env.imbalance > 0) {

			/* Prevent to re-select dst_cpu via env's cpus */
			cpumask_clear_cpu(env.dst_cpu, env.cpus);

			env.dst_rq	 = cpu_rq(env.new_dst_cpu);
			env.dst_cpu	 = env.new_dst_cpu;
			env.flags	&= ~LBF_DST_PINNED;
			env.loop	 = 0;
			env.loop_break	 = sched_nr_migrate_break;

			/*
			 * Go back to "more_balance" rather than "redo" since we
			 * need to continue with same src_cpu.
			 */
			goto more_balance;
		}

		/*
		 * We failed to reach balance because of affinity.
		 */
		if (sd_parent) {
			int *group_imbalance = &sd_parent->groups->sgc->imbalance;

			if ((env.flags & LBF_SOME_PINNED) && env.imbalance > 0)
				*group_imbalance = 1;
		}

		/* All tasks on this runqueue were pinned by CPU affinity */
		if (unlikely(env.flags & LBF_ALL_PINNED)) {
		    /* 从候选 cpu 列表中清除当前选择的目的 cpu 并尝试重新执行负载
		       均衡任务迁移操作 */
			cpumask_clear_cpu(cpu_of(busiest), cpus);
			if (!cpumask_empty(cpus)) {
				env.loop = 0;
				env.loop_break = sched_nr_migrate_break;
				goto redo;
			}
			goto out_all_pinned;
		}
	}

	if (!ld_moved) {
		schedstat_inc(sd, lb_failed[idle]);
		/*
		 * Increment the failure counter only on periodic balance.
		 * We do not want newidle balance, which can be very
		 * frequent, pollute the failure counter causing
		 * excessive cache_hot migrations and active balances.
		 */
		if (idle != CPU_NEWLY_IDLE)
			sd->nr_balance_failed++;

        /* 如果一个任务都没有迁移成功并且当前需要执行负载均衡任务迁移，则执行下面的操作 */
		if (need_active_balance(&env)) {
			raw_spin_lock_irqsave(&busiest->lock, flags);

			/* don't kick the active_load_balance_cpu_stop,
			 * if the curr task on busiest cpu can't be
			 * moved to this_cpu
			 */
			if (!cpumask_test_cpu(this_cpu,
					tsk_cpus_allowed(busiest->curr))) {
				raw_spin_unlock_irqrestore(&busiest->lock,
							    flags);
				env.flags |= LBF_ALL_PINNED;
				goto out_one_pinned;
			}

			/*
			 * ->active_balance synchronizes accesses to
			 * ->active_balance_work.  Once set, it's cleared
			 * only after active load balance is finished.
			 */
			if (!busiest->active_balance) {
				busiest->active_balance = 1;
				busiest->push_cpu = this_cpu;
				active_balance = 1;
			}
			raw_spin_unlock_irqrestore(&busiest->lock, flags);

			if (active_balance) {
				stop_one_cpu_nowait(cpu_of(busiest),
					active_load_balance_cpu_stop, busiest,
					&busiest->active_balance_work);
			}

			/*
			 * We've kicked active balancing, reset the failure
			 * counter.
			 */
			sd->nr_balance_failed = sd->cache_nice_tries+1;
		}
	} else
		sd->nr_balance_failed = 0;

	if (likely(!active_balance)) {
		/* We were unbalanced, so reset the balancing interval */
		sd->balance_interval = sd->min_interval;
	} else {
		/*
		 * If we've begun active balancing, start to back off. This
		 * case may not be covered by the all_pinned logic if there
		 * is only 1 task on the busy runqueue (because we don't call
		 * detach_tasks).
		 */
		if (sd->balance_interval < sd->max_interval)
			sd->balance_interval *= 2;
	}

	goto out;

out_balanced:
	/*
	 * We reach balance although we may have faced some affinity
	 * constraints. Clear the imbalance flag if it was set.
	 */
	if (sd_parent) {
		int *group_imbalance = &sd_parent->groups->sgc->imbalance;

		if (*group_imbalance)
			*group_imbalance = 0;
	}

out_all_pinned:
	/*
	 * We reach balance because all tasks are pinned at this level so
	 * we can't migrate them. Let the imbalance flag set so parent level
	 * can try to migrate them.
	 */
	schedstat_inc(sd, lb_balanced[idle]);

	sd->nr_balance_failed = 0;

out_one_pinned:
	/* tune up the balancing interval */
	if (((env.flags & LBF_ALL_PINNED) &&
			sd->balance_interval < MAX_PINNED_INTERVAL) ||
			(sd->balance_interval < sd->max_interval))
		sd->balance_interval *= 2;

	ld_moved = 0;
out:
	return ld_moved;
}

/*********************************************************************************************************
** 函数名称: get_sd_balance_interval
** 功能描述: 获取指定调度域内的负载均衡操作周期时间间隔，单位是 jiffies 周期数
** 输	 入: sd - 指定的调度域指针
**         : cpu_busy - 表示当前正在运行的 cpu 是否忙
** 输	 出: interval - 执行负载均衡操作周期时间间隔，单位是 jiffies 周期数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long
get_sd_balance_interval(struct sched_domain *sd, int cpu_busy)
{
	unsigned long interval = sd->balance_interval;

	if (cpu_busy)
		interval *= sd->busy_factor;

	/* scale ms to jiffies */
	interval = msecs_to_jiffies(interval);
	interval = clamp(interval, 1UL, max_load_balance_interval);

	return interval;
}

/*********************************************************************************************************
** 函数名称: update_next_balance
** 功能描述: 尝试把指定的调度域的 next_balance 值提前到计算出的 next 值位置
** 输	 入: sd - 指定的调度域指针
**         : cpu_busy - 表示当前正在运行的 cpu 是否忙
**         : next_balance - 指定的 next_balance 变量指针
** 输	 出: next_balance - 指定的 next_balance 变量指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
update_next_balance(struct sched_domain *sd, int cpu_busy, unsigned long *next_balance)
{
	unsigned long interval, next;

	interval = get_sd_balance_interval(sd, cpu_busy);
	next = sd->last_balance + interval;

    /* 把指定的 next_balance 值提前到 next 指定的位置 */
	if (time_after(*next_balance, next))
		*next_balance = next;
}

/*
 * idle_balance is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
/*********************************************************************************************************
** 函数名称: idle_balance
** 功能描述: 当 cpu 处于 idle 状态下时尝试从其他最忙 cpu 上迁移任务到当前的 idle cpu 上使系统负载均衡 
** 输	 入: this_rq - 指定的 idle cpu 的 cpu 运行队列指针
** 输	 出: pulled_task - 成功迁移到当前 cpu 上的任务数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int idle_balance(struct rq *this_rq)
{
	unsigned long next_balance = jiffies + HZ;
	int this_cpu = this_rq->cpu;
	struct sched_domain *sd;
	int pulled_task = 0;
	
	/* 统计本次负载均衡操作一共消耗的时间，单位是 ns */
	u64 curr_cost = 0;

    /* 在进入 idle 进程之前调用，用来更新指定的 cpu 运行队列的 runnable_avg 贡献值信息 */
	idle_enter_fair(this_rq);

	/*
	 * We must set idle_stamp _before_ calling idle_balance(), such that we
	 * measure the duration of idle_balance() as idle time.
	 */
	this_rq->idle_stamp = rq_clock(this_rq);

    /* 如果当前 cpu 处于 idle 状态并且当前 cpu 的负载较低，则尝试提高执行负载均衡的频率 */
	if (this_rq->avg_idle < sysctl_sched_migration_cost ||
	    !this_rq->rd->overload) {
		rcu_read_lock();
		sd = rcu_dereference_check_sched_domain(this_rq->sd);
		if (sd)
			update_next_balance(sd, 0, &next_balance);
		rcu_read_unlock();

		goto out;
	}

	/*
	 * Drop the rq->lock, but keep IRQ/preempt disabled.
	 */
	raw_spin_unlock(&this_rq->lock);

	/* 更新指定的 cpu 运行队列上每一个任务组叶子节点的 cfs 运行队列的阻塞负载贡献统计值 */
	update_blocked_averages(this_cpu);
	
	rcu_read_lock();
	/* 从指定的 cpu 的调度域开始到所属调度域树根节点遍历路径中每一个调度域，尝试从其它 cpu
	   上迁移任务到当前处于 idle 状态的 cpu 上 */
	for_each_domain(this_cpu, sd) {
		int continue_balancing = 1;
		u64 t0, domain_cost;

        /* 如果当前调度域不可以执行负载均衡操作，则直接跳过 */
		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		if (this_rq->avg_idle < curr_cost + sd->max_newidle_lb_cost) {
			update_next_balance(sd, 0, &next_balance);
			break;
		}

		if (sd->flags & SD_BALANCE_NEWIDLE) {
			t0 = sched_clock_cpu(this_cpu);

            /* 尝试从指定的调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列上
               使这两个 cpu 运行队列的负载处于平衡状态 */
			pulled_task = load_balance(this_cpu, this_rq,
						   sd, CPU_NEWLY_IDLE,
						   &continue_balancing);

			domain_cost = sched_clock_cpu(this_cpu) - t0;
			if (domain_cost > sd->max_newidle_lb_cost)
				sd->max_newidle_lb_cost = domain_cost;

			curr_cost += domain_cost;
		}

		update_next_balance(sd, 0, &next_balance);

		/*
		 * Stop searching for tasks to pull if there are
		 * now runnable tasks on this rq.
		 */
		if (pulled_task || this_rq->nr_running > 0)
			break;
	}
	rcu_read_unlock();

	raw_spin_lock(&this_rq->lock);

	if (curr_cost > this_rq->max_idle_balance_cost)
		this_rq->max_idle_balance_cost = curr_cost;

	/*
	 * While browsing the domains, we released the rq lock, a task could
	 * have been enqueued in the meantime. Since we're not going idle,
	 * pretend we pulled a task.
	 */
	if (this_rq->cfs.h_nr_running && !pulled_task)
		pulled_task = 1;

out:
	/* Move the next balance forward */
	if (time_after(this_rq->next_balance, next_balance))
		this_rq->next_balance = next_balance;

	/* Is there a task of a high priority class? */
	if (this_rq->nr_running != this_rq->cfs.h_nr_running)
		pulled_task = -1;

	if (pulled_task) {
		/* 在退出 idle 进程之前调用，用来更新指定的 cpu 运行队列的 runnable_avg 贡献值信息 */
		idle_exit_fair(this_rq);
		this_rq->idle_stamp = 0;
	}

	return pulled_task;
}

/*
 * active_load_balance_cpu_stop is run by cpu stopper. It pushes
 * running tasks off the busiest CPU onto idle CPUs. It requires at
 * least 1 task to be running on each physical CPU where possible, and
 * avoids physical / logical imbalances.
 */
/*********************************************************************************************************
** 函数名称: active_load_balance_cpu_stop
** 功能描述: 在 cpu stop 状态下尝试从指定的最忙 cpu 运行队列上迁移一个任务到指定的目的 cpu 运行队列上
** 输	 入: data - 指定的最忙 cpu 运行队列指针
** 输	 出: 0 - 执行完成
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int active_load_balance_cpu_stop(void *data)
{
	struct rq *busiest_rq = data;
	int busiest_cpu = cpu_of(busiest_rq);
	int target_cpu = busiest_rq->push_cpu;
	struct rq *target_rq = cpu_rq(target_cpu);
	struct sched_domain *sd;
	struct task_struct *p = NULL;

	raw_spin_lock_irq(&busiest_rq->lock);

	/* make sure the requested cpu hasn't gone down in the meantime */
	if (unlikely(busiest_cpu != smp_processor_id() ||
		     !busiest_rq->active_balance))
		goto out_unlock;

	/* Is there any task to move? */
	if (busiest_rq->nr_running <= 1)
		goto out_unlock;

	/*
	 * This condition is "impossible", if it occurs
	 * we need to fix it. Originally reported by
	 * Bjorn Helgaas on a 128-cpu setup.
	 */
	BUG_ON(busiest_rq == target_rq);

	/* Search for an sd spanning us and the target CPU. */
	rcu_read_lock();

	/* 遍历指定的目的 cpu 所属调度域到所属调度域树根节点遍历路径找到第一个可以执行
	   负载均衡任务迁移并且同时包含目的 cpu 和最忙 cpu 的调度域 */
	for_each_domain(target_cpu, sd) {
		if ((sd->flags & SD_LOAD_BALANCE) &&
		    cpumask_test_cpu(busiest_cpu, sched_domain_span(sd)))
				break;
	}

	if (likely(sd)) {
		struct lb_env env = {
			.sd		= sd,
			.dst_cpu	= target_cpu,
			.dst_rq		= target_rq,
			.src_cpu	= busiest_rq->cpu,
			.src_rq		= busiest_rq,
			.idle		= CPU_IDLE,
		};

		schedstat_inc(sd, alb_count);

		p = detach_one_task(&env);
		if (p)
			schedstat_inc(sd, alb_pushed);
		else
			schedstat_inc(sd, alb_failed);
	}

	rcu_read_unlock();
	
out_unlock:
	busiest_rq->active_balance = 0;
	raw_spin_unlock(&busiest_rq->lock);

	if (p)
		attach_one_task(target_rq, p);

	local_irq_enable();

	return 0;
}

/*********************************************************************************************************
** 函数名称: on_null_domain
** 功能描述: 判断指定的 cpu 运行队列是否挂载在 NULL 调度域上
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int on_null_domain(struct rq *rq)
{
	return unlikely(!rcu_dereference_sched(rq->sd));
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * idle load balancing details
 * - When one of the busy CPUs notice that there may be an idle rebalancing
 *   needed, they will kick the idle load balancer, which then does idle
 *   load balancing for all the idle CPUs.
 */
/* 空闲负载均衡详细描述：
   当一个繁忙的 cpu 注意到可能需要一个空闲的负载均衡时，它将启动空闲负载平衡器
   空闲负载平衡器将会为所有处于空闲状态的 cpu 执行空闲负载均衡操作 */

static struct {
    /* 表示当前系统内处于空闲状态的 cpu 的位图掩码值 */
	cpumask_var_t idle_cpus_mask;

	/* 表示当前系统内处于空闲状态的 cpu 的个数 */
	atomic_t nr_cpus;
	
	unsigned long next_balance;     /* in jiffy units */
} nohz ____cacheline_aligned;

/*********************************************************************************************************
** 函数名称: find_new_ilb
** 功能描述: 查找当前系统内第一个处于空闲状态的 cpu 的 id 值
** 输	 入: 
** 输	 出: ilb - 空闲状态的 cpu 的 id 值
**         : nr_cpu_ids - 没找到空闲状态的 cpu
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int find_new_ilb(void)
{
	int ilb = cpumask_first(nohz.idle_cpus_mask);

	if (ilb < nr_cpu_ids && idle_cpu(ilb))
		return ilb;

	return nr_cpu_ids;
}

/*
 * Kick a CPU to do the nohz balancing, if it is time for it. We pick the
 * nohz_load_balancer CPU (if there is one) otherwise fallback to any idle
 * CPU (if there is one).
 */
/*********************************************************************************************************
** 函数名称: nohz_balancer_kick
** 功能描述: 尝试向当前系统内处于 idle 状态的 cpu 发送一个核间中断使其执行一次 nohz 负载均衡操作
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void nohz_balancer_kick(void)
{
	int ilb_cpu;

	nohz.next_balance++;

	ilb_cpu = find_new_ilb();

	if (ilb_cpu >= nr_cpu_ids)
		return;

	if (test_and_set_bit(NOHZ_BALANCE_KICK, nohz_flags(ilb_cpu)))
		return;
	/*
	 * Use smp_send_reschedule() instead of resched_cpu().
	 * This way we generate a sched IPI on the target cpu which
	 * is idle. And the softirq performing nohz idle load balance
	 * will be run before returning from the IPI.
	 */
	/* 向指定的 ilb_cpu 上发送一个 IPI_RESCHEDULE 核间中断 */
	smp_send_reschedule(ilb_cpu);
	return;
}

/*********************************************************************************************************
** 函数名称: nohz_balance_exit_idle
** 功能描述: 在指定的处于 idle 状态的 cpu 执行完 nohz 负载均衡后退出 idle 状态时调用，用来更新系统中
**         : 相关的数据状态和标志
** 输	 入: cpu - 指定的处于 idle 状态的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void nohz_balance_exit_idle(int cpu)
{
	if (unlikely(test_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu)))) {
		/*
		 * Completely isolated CPUs don't ever set, so we must test.
		 */
		if (likely(cpumask_test_cpu(cpu, nohz.idle_cpus_mask))) {
			cpumask_clear_cpu(cpu, nohz.idle_cpus_mask);
			atomic_dec(&nohz.nr_cpus);
		}
		clear_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu));
	}
}

/*********************************************************************************************************
** 函数名称: set_cpu_sd_state_busy
** 功能描述: 把当前正在运行的 cpu 的调度域从 nohz idle 状态退出并设置为忙状态
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void set_cpu_sd_state_busy(void)
{
	struct sched_domain *sd;
	int cpu = smp_processor_id();

	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_busy, cpu));

	if (!sd || !sd->nohz_idle)
		goto unlock;
	
	sd->nohz_idle = 0;

	atomic_inc(&sd->groups->sgc->nr_busy_cpus);
unlock:
	rcu_read_unlock();
}

/*********************************************************************************************************
** 函数名称: set_cpu_sd_state_idle
** 功能描述: 设置当前正在运行的 cpu 的调度域为 nohz idle 状态
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void set_cpu_sd_state_idle(void)
{
	struct sched_domain *sd;
	int cpu = smp_processor_id();

	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_busy, cpu));

	if (!sd || sd->nohz_idle)
		goto unlock;
	sd->nohz_idle = 1;

	atomic_dec(&sd->groups->sgc->nr_busy_cpus);
unlock:
	rcu_read_unlock();
}

/*
 * This routine will record that the cpu is going idle with tick stopped.
 * This info will be used in performing idle load balancing in the future.
 */
/*********************************************************************************************************
** 函数名称: nohz_balance_enter_idle
** 功能描述: 在 tick 中断结束时如果指定的 cpu 进入了 idle 状态，则调用这个函数更新其状态为 idle，这样
**         : 我们将来就可以在这个 cpu 上执行 idle 负载均衡操作了
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void nohz_balance_enter_idle(int cpu)
{
	/*
	 * If this cpu is going down, then nothing needs to be done.
	 */
	if (!cpu_active(cpu))
		return;

	if (test_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu)))
		return;

	/*
	 * If we're a completely isolated CPU, we don't play.
	 */
	if (on_null_domain(cpu_rq(cpu)))
		return;

	cpumask_set_cpu(cpu, nohz.idle_cpus_mask);
	atomic_inc(&nohz.nr_cpus);
	set_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu));
}

/*********************************************************************************************************
** 函数名称: sched_ilb_notifier
** 功能描述: 用来通知系统当前正在运行的 cpu 将会退出 nohz 负载均衡的 idle 状态
** 输	 入: nfb - 不使用
**         : action - 指定的动作类型
**         : hcpu - 不使用
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int sched_ilb_notifier(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DYING:
		nohz_balance_exit_idle(smp_processor_id());
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}
#endif

static DEFINE_SPINLOCK(balancing);

/*
 * Scale the max load_balance interval with the number of CPUs in the system.
 * This trades load-balance latency on larger machines for less cross talk.
 */
/*********************************************************************************************************
** 函数名称: update_max_interval
** 功能描述: 根据当前系统内存在的 online 状态 cpu 个数更新最大负载均衡间隔时间
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void update_max_interval(void)
{
	max_load_balance_interval = HZ*num_online_cpus()/10;
}

/*
 * It checks each scheduling domain to see if it is due to be balanced,
 * and initiates a balancing operation if so.
 *
 * Balancing parameters are set up in init_sched_domains.
 */
/*********************************************************************************************************
** 函数名称: rebalance_domains
** 功能描述: 从指定的当前 cpu 的调度域开始到所属调度域树根节点遍历路径中每一个调度域，尝试从遍历的
**         : 调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列上，使这两个 cpu 运行队列的负
**         : 载处于平衡状态
** 输	 入: rq - 指定的当前 cpu 运行队列指针
**         : idle - 指定的当前 cpu idle 类型
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void rebalance_domains(struct rq *rq, enum cpu_idle_type idle)
{
	int continue_balancing = 1;
	int cpu = rq->cpu;
	unsigned long interval;
	struct sched_domain *sd;
	/* Earliest time when we have to do rebalance again */
	unsigned long next_balance = jiffies + 60*HZ;
	int update_next_balance = 0;
	int need_serialize, need_decay = 0;
	u64 max_cost = 0;

    /* 更新指定的 cpu 运行队列上每一个任务组叶子节点的 cfs 运行队列的阻塞负载贡献统计值 */
	update_blocked_averages(cpu);

	rcu_read_lock();
	/* 从指定的 cpu 的调度域开始到所属调度域树根节点遍历路径中每一个调度域，尝试从遍历的调度域内
	   最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列上，使这两个 cpu 运行队列的负载处于平衡状态 */
	for_each_domain(cpu, sd) {
		/*
		 * Decay the newidle max times here because this is a regular
		 * visit to all the domains. Decay ~1% per second.
		 */
		if (time_after(jiffies, sd->next_decay_max_lb_cost)) {
			sd->max_newidle_lb_cost =
				(sd->max_newidle_lb_cost * 253) / 256;
			sd->next_decay_max_lb_cost = jiffies + HZ;
			need_decay = 1;
		}
		max_cost += sd->max_newidle_lb_cost;

		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		/*
		 * Stop the load balance at this level. There is another
		 * CPU in our sched group which is doing load balancing more
		 * actively.
		 */
		if (!continue_balancing) {
			if (need_decay)
				continue;
			break;
		}

		interval = get_sd_balance_interval(sd, idle != CPU_IDLE);

		need_serialize = sd->flags & SD_SERIALIZE;
		if (need_serialize) {
			if (!spin_trylock(&balancing))
				goto out;
		}

		if (time_after_eq(jiffies, sd->last_balance + interval)) {
			if (load_balance(cpu, rq, sd, idle, &continue_balancing)) {
				/*
				 * The LBF_DST_PINNED logic could have changed
				 * env->dst_cpu, so we can't know our idle
				 * state even if we migrated tasks. Update it.
				 */
				idle = idle_cpu(cpu) ? CPU_IDLE : CPU_NOT_IDLE;
			}
			sd->last_balance = jiffies;
			interval = get_sd_balance_interval(sd, idle != CPU_IDLE);
		}
		
		if (need_serialize)
			spin_unlock(&balancing);
out:
		if (time_after(next_balance, sd->last_balance + interval)) {
			next_balance = sd->last_balance + interval;
			update_next_balance = 1;
		}
	}
	
	if (need_decay) {
		/*
		 * Ensure the rq-wide value also decays but keep it at a
		 * reasonable floor to avoid funnies with rq->avg_idle.
		 */
		rq->max_idle_balance_cost =
			max((u64)sysctl_sched_migration_cost, max_cost);
	}
	rcu_read_unlock();

	/*
	 * next_balance will be updated only when there is a need.
	 * When the cpu is attached to null domain for ex, it will not be
	 * updated.
	 */
	if (likely(update_next_balance))
		rq->next_balance = next_balance;
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * In CONFIG_NO_HZ_COMMON case, the idle balance kickee will do the
 * rebalancing for all the cpus for whom scheduler ticks are stopped.
 */
/*********************************************************************************************************
** 函数名称: nohz_idle_balance
** 功能描述: 根据当前正在运行的 cpu 状态尝试遍历 nohz.idle_cpus_mask 看是否存在当前正在运行的 cpu
**         : 如果存在则尝试从当前 cpu 所属调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列
**         : 上，使这两个 cpu 运行队列的负载处于平衡状态
** 输	 入: rq - 指定的当前 cpu 运行队列指针
**         : idle - 指定的当前 cpu idle 类型
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void nohz_idle_balance(struct rq *this_rq, enum cpu_idle_type idle)
{
	int this_cpu = this_rq->cpu;
	struct rq *rq;
	int balance_cpu;

	if (idle != CPU_IDLE ||
	    !test_bit(NOHZ_BALANCE_KICK, nohz_flags(this_cpu)))
		goto end;

    /* 遍历 nohz.idle_cpus_mask 看是否存在当前正在运行的 cpu，如果存在则尝试从当前 cpu 所属调度域内
       最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列上，使这两个 cpu 运行队列的负载处于平衡状态 */
	for_each_cpu(balance_cpu, nohz.idle_cpus_mask) {
		if (balance_cpu == this_cpu || !idle_cpu(balance_cpu))
			continue;

		/*
		 * If this cpu gets work to do, stop the load balancing
		 * work being done for other cpus. Next load
		 * balancing owner will pick it up.
		 */
		if (need_resched())
			break;

		rq = cpu_rq(balance_cpu);

		/*
		 * If time for next balance is due,
		 * do the balance.
		 */
		if (time_after_eq(jiffies, rq->next_balance)) {
		
		    /* 更新指定的处于 idle 状态的 cpu 运行队列时钟和负载贡献值 */
			raw_spin_lock_irq(&rq->lock);
			update_rq_clock(rq);
			update_idle_cpu_load(rq);
			raw_spin_unlock_irq(&rq->lock);
			
			/* 从指定的当前 cpu 的调度域开始到所属调度域树根节点遍历路径中每一个
			   调度域，尝试从遍历的调度域内最忙 cpu 运行队列中迁移任务到当前 cpu
			   的运行队列上，使这两个 cpu 运行队列的负载处于平衡状态 */
			rebalance_domains(rq, CPU_IDLE);
		}

		if (time_after(this_rq->next_balance, rq->next_balance))
			this_rq->next_balance = rq->next_balance;
	}
	
	nohz.next_balance = this_rq->next_balance;
end:
	clear_bit(NOHZ_BALANCE_KICK, nohz_flags(this_cpu));
}

/*
 * Current heuristic for kicking the idle load balancer in the presence
 * of an idle cpu is the system.
 *   - This rq has more than one task.
 *   - At any scheduler domain level, this cpu's scheduler group has multiple
 *     busy cpu's exceeding the group's capacity.
 *   - For SD_ASYM_PACKING, if the lower numbered cpu's in the scheduler
 *     domain span are idle.
 */
/*********************************************************************************************************
** 函数名称: nohz_kick_needed
** 功能描述: 判断指定的 cpu 运行队列是否需要执行 idle 负载均衡操作
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 1 - 需要执行 idle 负载均衡操作
**         : 0 - 不需要执行 idle 负载均衡操作
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int nohz_kick_needed(struct rq *rq)
{
	unsigned long now = jiffies;
	struct sched_domain *sd;
	struct sched_group_capacity *sgc;
	int nr_busy, cpu = rq->cpu;

	if (unlikely(rq->idle_balance))
		return 0;

   /*
	* We may be recently in ticked or tickless idle mode. At the first
	* busy tick after returning from idle, we will update the busy stats.
	*/
    /* 把当前正在运行的 cpu 的调度域从 nohz idle 状态退出并设置为忙状态 */
	set_cpu_sd_state_busy();

	/* 在指定的处于 idle 状态的 cpu 执行完 nohz 负载均衡后退出 idle 状态时
	   调用，用来更新系统中相关的数据状态和标志 */
	nohz_balance_exit_idle(cpu);

	/*
	 * None are in tickless mode and hence no need for NOHZ idle load
	 * balancing.
	 */
	if (likely(!atomic_read(&nohz.nr_cpus)))
		return 0;

	if (time_before(now, nohz.next_balance))
		return 0;

	if (rq->nr_running >= 2)
		goto need_kick;

	rcu_read_lock();
	sd = rcu_dereference(per_cpu(sd_busy, cpu));

	if (sd) {
		sgc = sd->groups->sgc;
		nr_busy = atomic_read(&sgc->nr_busy_cpus);

		if (nr_busy > 1)
			goto need_kick_unlock;
	}

	sd = rcu_dereference(per_cpu(sd_asym, cpu));

	if (sd && (cpumask_first_and(nohz.idle_cpus_mask,
				  sched_domain_span(sd)) < cpu))
		goto need_kick_unlock;

	rcu_read_unlock();
	return 0;

need_kick_unlock:
	rcu_read_unlock();
need_kick:
	return 1;
}
#else
/*********************************************************************************************************
** 函数名称: nohz_idle_balance
** 功能描述: 根据当前正在运行的 cpu 状态尝试遍历 nohz.idle_cpus_mask 看是否存在当前正在运行的 cpu
**         : 如果存在则尝试从当前 cpu 所属调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列
**         : 上，使这两个 cpu 运行队列的负载处于平衡状态
** 输	 入: rq - 指定的当前 cpu 运行队列指针
**         : idle - 指定的当前 cpu idle 类型
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void nohz_idle_balance(struct rq *this_rq, enum cpu_idle_type idle) { }
#endif

/*
 * run_rebalance_domains is triggered when needed from the scheduler tick.
 * Also triggered for nohz idle balancing (with nohz_balancing_kick set).
 */
/*********************************************************************************************************
** 函数名称: run_rebalance_domains
** 功能描述: 尝试在当前正在运行的 cpu 所属调度域内执行负载均衡操作
** 注     释: 这个函数由 trigger_load_balance 函数通过软中断方式触发调用
** 输	 入: h - 未使用
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void run_rebalance_domains(struct softirq_action *h)
{
	struct rq *this_rq = this_rq();
	enum cpu_idle_type idle = this_rq->idle_balance ?
						CPU_IDLE : CPU_NOT_IDLE;

    /* 从指定的当前 cpu 的调度域开始到所属调度域树根节点遍历路径中每一个调度域，尝试从遍历的
       调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列上，使这两个 cpu 运行队列的负
       载处于平衡状态 */
	rebalance_domains(this_rq, idle);

	/*
	 * If this cpu has a pending nohz_balance_kick, then do the
	 * balancing on behalf of the other idle cpus whose ticks are
	 * stopped.
	 */
	/* 根据当前正在运行的 cpu 状态尝试遍历 nohz.idle_cpus_mask 看是否存在当前正在运行的 cpu
       如果存在则尝试从当前 cpu 所属调度域内最忙 cpu 运行队列中迁移任务到当前 cpu 的运行队列
       上，使这两个 cpu 运行队列的负载处于平衡状态 */
	nohz_idle_balance(this_rq, idle);
}

/*
 * Trigger the SCHED_SOFTIRQ if it is time to do periodic load balancing.
 */
/*********************************************************************************************************
** 函数名称: trigger_load_balance
** 功能描述: 尝试在执行的 cpu 运行队列所属 cpu 上触发一次负载均衡操作
** 注     释: 这个函数会在 scheduler_tick 中周期性调用
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void trigger_load_balance(struct rq *rq)
{
	/* Don't need to rebalance while attached to NULL domain */
	if (unlikely(on_null_domain(rq)))
		return;

	if (time_after_eq(jiffies, rq->next_balance))
		raise_softirq(SCHED_SOFTIRQ);

#ifdef CONFIG_NO_HZ_COMMON
	if (nohz_kick_needed(rq))
		nohz_balancer_kick();
#endif
}

/*********************************************************************************************************
** 函数名称: rq_offline_fair
** 功能描述: 根据转换到 online 状态的 cpu 运行队列上的任务组的带宽控制状态更新对应的 cfs_rq->runtime_enabled
**         : 成员值以及系统中与其相关的调度控制参数
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void rq_online_fair(struct rq *rq)
{
	update_sysctl();

	update_runtime_enabled(rq);
}

/*********************************************************************************************************
** 函数名称: rq_offline_fair
** 功能描述: 对转换到 offline 状态的 cpu 运行队列中处于 throttled 状态的 cfs 运行队列执行 unthrottle
**         : 操作并更新系统中与其相关的调度控制参数
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void rq_offline_fair(struct rq *rq)
{
	update_sysctl();

	/* Ensure any throttled groups are reachable by pick_next_task */
	unthrottle_offline_cfs_rqs(rq);
}

#endif /* CONFIG_SMP */

/*
 * scheduler tick hitting a task of our scheduling class:
 */
/*********************************************************************************************************
** 函数名称: task_tick_fair
** 功能描述: 当前系统 cfs 调度类的 tick 处理函数，在 cpu 正在运行的任务是 cfs 任务时调用，操作如下：
**         : 1. 对当前正在运行的调度实例执行周期性操作，用来更新调度实例运行时统计信息
**         : 2. 检查当前 cpu 运行队列的 numa 扫描周期时间是否已经到达，如果已经到达了则执行相关的扫描操作
**         : 3. 更新当前 cpu 运行队列的 runnable_avg 贡献值信息，包括任务和任务组的
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : curr - 当前正在运行的 cfs 任务指针
**         : queued - 当前 tick 是否为 queued ticks
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_tick_fair(struct rq *rq, struct task_struct *curr, int queued)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

    /* 更新当前正在运行的调度任务或者调度任务组的运行时统计信息 */
	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		entity_tick(cfs_rq, se, queued);
	}

	if (numabalancing_enabled)
		task_tick_numa(rq, curr);

	update_rq_runnable_avg(rq, 1);
}

/*
 * called on fork with the child task as argument from the parent's context
 *  - child not yet on the tasklist
 *  - preemption disabled
 */
/*********************************************************************************************************
** 函数名称: task_tick_fair
** 功能描述: 当前系统 cfs 调度类的 fork 处理函数，在系统 fork 一个 cfs 任务时调用
** 输	 入: p - 指定的“子任务”指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_fork_fair(struct task_struct *p)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se, *curr;
	int this_cpu = smp_processor_id();
	struct rq *rq = this_rq();
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);

	update_rq_clock(rq);

    /* 表示父任务所属 cfs 运行队列以及父任务所属 cfs 运行队列当前正在运行的任务指针 */
	cfs_rq = task_cfs_rq(current);
	curr = cfs_rq->curr;

	/*
	 * Not only the cpu but also the task_group of the parent might have
	 * been changed after parent->se.parent,cfs_rq were copied to
	 * child->se.parent,cfs_rq. So call __set_task_cpu() to make those
	 * of child point to valid ones.
	 */
	/* 为子任务分配 cpu */
	rcu_read_lock();
	__set_task_cpu(p, this_cpu);
	rcu_read_unlock();

    /* 更新父任务的 cfs 运行队列中当前正在运行的调度实例的运行时统计信息*/
	update_curr(cfs_rq);

    /* 为了防止子任务在刚创建后长时间独占 cfs 运行队列，使其继承父任务的虚拟运行时间 */
	if (curr)
		se->vruntime = curr->vruntime;
	
	place_entity(cfs_rq, se, 1);

	if (sysctl_sched_child_runs_first && curr && entity_before(curr, se)) {
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		swap(curr->vruntime, se->vruntime);
		resched_curr(rq);
	}

    /* 在 place_entity 函数中我们是以当前正在运行的 cpu 的 min_vruntime 作为子任务的
       虚拟时间补偿，且每个 cpu 都有自己的运行队列，每个队列中的进程的 vruntime 也走
       得有快有慢，如果一个进程从 min_vruntime 更小的 cpu(A) 上迁移到 min_vruntime 
       更大的 cpu(B) 上，可能就会占便宜了，因为 cpu(B) 的运行队列中进程的 vruntime
       普遍比较大，迁移过来的进程就会获得更多的 cpu 时间片，为了处理不同 cpu 运行队列
       之间存在的虚拟运行时间差异，我们在一个任务 dequeue_entity 的时候会减去这个 cpu
       运行队列的 min_vruntime，而在 enqueue_entity 的时候会再加上新的 cpu 运行队列的
       min_vruntime，因为新创建的子任务还没分配 cpu 运行队列，所以这个位置要减去当前
       cpu 运行队列的 min_vruntime */
	se->vruntime -= cfs_rq->min_vruntime;

	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Priority of the task has changed. Check to see if we preempt
 * the current task.
 */
/*********************************************************************************************************
** 函数名称: task_tick_fair
** 功能描述: 当前系统 cfs 调度类的 prio_changed 处理函数，在 cfs 任务修改优先级“后”调用，用来判断
**         : 是否需要抢占当前正在运行的任务
** 输	 入: rq - 指定的任务所属 cpu 运行队列指针
**         : p - 指定的任务指针
**         : oldprio - 指定的任务旧的优先级
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void
prio_changed_fair(struct rq *rq, struct task_struct *p, int oldprio)
{
	if (!task_on_rq_queued(p))
		return;

	/*
	 * Reschedule if we are currently running on this runqueue and
	 * our priority decreased, or if we are not currently running on
	 * this runqueue and our priority is higher than the current's
	 */
	if (rq->curr == p) {
		if (p->prio > oldprio)
			resched_curr(rq);
	} else
		check_preempt_curr(rq, p, 0);
}

/*********************************************************************************************************
** 函数名称: switched_from_fair
** 功能描述: 当前系统 cfs 调度类的 switched_from 处理函数，在从 cfs 调度类切换到其他调度类“前”调用
**         : 用来同步更新指定任务的虚拟时间信息和负载贡献信息
** 输	 入: rq - 指定的任务所属 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void switched_from_fair(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	/*
	 * Ensure the task's vruntime is normalized, so that when it's
	 * switched back to the fair class the enqueue_entity(.flags=0) will
	 * do the right thing.
	 *
	 * If it's queued, then the dequeue_entity(.flags=0) will already
	 * have normalized the vruntime, if it's !queued, then only when
	 * the task is sleeping will it still have non-normalized vruntime.
	 */
	if (!task_on_rq_queued(p) && p->state != TASK_RUNNING) {
		/*
		 * Fix up our vruntime so that the current sleep doesn't
		 * cause 'unlimited' sleep bonus.
		 */
		place_entity(cfs_rq, se, 0);
		se->vruntime -= cfs_rq->min_vruntime;
	}

#ifdef CONFIG_SMP
	/*
	* Remove our load from contribution when we leave sched_fair
	* and ensure we don't carry in an old decay_count if we
	* switch back.
	*/
	if (se->avg.decay_count) {
		__synchronize_entity_decay(se);
		subtract_blocked_load_contrib(cfs_rq, se->avg.load_avg_contrib);
	}
#endif
}

/*
 * We switched to the sched_fair class.
 */
/*********************************************************************************************************
** 函数名称: switched_from_fair
** 功能描述: 当前系统 cfs 调度类的 switched_to 处理函数，在从其他调度类切换到 cfs 任务“后”调用
**         : 用来判断是否需要执行任务调度，如果需要，则尝试执行一次任务调度
** 输	 入: rq - 指定的任务所属 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void switched_to_fair(struct rq *rq, struct task_struct *p)
{
#ifdef CONFIG_FAIR_GROUP_SCHED
	struct sched_entity *se = &p->se;
	/*
	 * Since the real-depth could have been changed (only FAIR
	 * class maintain depth value), reset depth properly.
	 */
	se->depth = se->parent ? se->parent->depth + 1 : 0;
#endif

	if (!task_on_rq_queued(p))
		return;

	/*
	 * We were most likely switched from sched_rt, so
	 * kick off the schedule if running, otherwise just see
	 * if we can still preempt the current task.
	 */
	if (rq->curr == p)
		resched_curr(rq);
	else
		check_preempt_curr(rq, p, 0);
}

/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
/*********************************************************************************************************
** 函数名称: set_curr_task_fair
** 功能描述: 当前系统 cfs 调度类的 set_curr_task 处理函数
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void set_curr_task_fair(struct rq *rq)
{
	struct sched_entity *se = &rq->curr->se;

	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);

		set_next_entity(cfs_rq, se);
		/* ensure bandwidth has been allocated on our new cfs_rq */
		account_cfs_rq_runtime(cfs_rq, 0);
	}
}

/*********************************************************************************************************
** 函数名称: init_cfs_rq
** 功能描述: 初始化指定的 cfs 运行队列数据结构
** 输	 入: cfs_rq - 指定的 cfs 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void init_cfs_rq(struct cfs_rq *cfs_rq)
{
	cfs_rq->tasks_timeline = RB_ROOT;
	cfs_rq->min_vruntime = (u64)(-(1LL << 20));
#ifndef CONFIG_64BIT
	cfs_rq->min_vruntime_copy = cfs_rq->min_vruntime;
#endif
#ifdef CONFIG_SMP
	atomic64_set(&cfs_rq->decay_counter, 1);
	atomic_long_set(&cfs_rq->removed_load, 0);
#endif
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/*********************************************************************************************************
** 函数名称: task_move_group_fair
** 功能描述: 在 cfs 调度类中的任务在任务组之间迁移时调用
** 输	 入: p - 指定的迁移任务指针
**         : queued - 指定的迁移任务是否在 cpu 运行队列上
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void task_move_group_fair(struct task_struct *p, int queued)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq;

	/*
	 * If the task was not on the rq at the time of this cgroup movement
	 * it must have been asleep, sleeping tasks keep their ->vruntime
	 * absolute on their old rq until wakeup (needed for the fair sleeper
	 * bonus in place_entity()).
	 *
	 * If it was on the rq, we've just 'preempted' it, which does convert
	 * ->vruntime to a relative base.
	 *
	 * Make sure both cases convert their relative position when migrating
	 * to another cgroup's rq. This does somewhat interfere with the
	 * fair sleeper stuff for the first placement, but who cares.
	 */
	/*
	 * When !queued, vruntime of the task has usually NOT been normalized.
	 * But there are some cases where it has already been normalized:
	 *
	 * - Moving a forked child which is waiting for being woken up by
	 *   wake_up_new_task().
	 * - Moving a task which has been woken up by try_to_wake_up() and
	 *   waiting for actually being woken up by sched_ttwu_pending().
	 *
	 * To prevent boost or penalty in the new cfs_rq caused by delta
	 * min_vruntime between the two cfs_rqs, we skip vruntime adjustment.
	 */
	/* 如果任务在这个任务组上移动的时候不在 cpu 运行队列上，那么它一定是处
	   于睡眠状态，睡眠任务保持它们原来旧的 ->vruntime，直到唤醒它们（我们
	   在 place_entity() 函数中需要公平奖励睡眠任务）

       如果任务在 cpu 运行队列上，我们只是“抢占”它，那么它会将 ->vruntime 
       转换成一个相对的基数值

       为了确保从一个任务组迁移到另一个任务组的 cpu 运行队列时虚拟时间的相
       计数值不变所做的操作在一定程度上会影响到 cpu 运行队列上第一个位置的
       睡眠者，但是谁在乎呢 */
    /* 在 queued 参数为 0 时，大多数任务的 vruntime 都是没有标准化的，但是
       在某些情况下，有的任务的 vruntime 已经被标准化了，具体如下：
       1. 正在移动的任务是一个正在等待 wake_up_new_task() 函数唤醒的、刚刚
          fork 的子任务
       2. 移动一个已经被 try_to_wake_up() 函数唤醒的任务，但是还在等待被
          sched_ttwu_pending() 函数唤醒的任务

       为了防止因为两个 cfs 运行队列的 min_vruntime 差异导致的虚拟时间奖励
       和补偿，我们跳过了 vruntime 的调整 */

    /* 处理上面描述的 1 和 2 两种情况 */
	if (!queued && (!se->sum_exec_runtime || p->state == TASK_WAKING))
		queued = 1;

    /* 下面的 !queued 逻辑分支是用来处理 vruntime 的标准化 */
	if (!queued)
		se->vruntime -= cfs_rq_of(se)->min_vruntime;

	/* 设置指定的任务的运行队列信息为其所在 cpu 的运行队列信息 */
	set_task_rq(p, task_cpu(p));
	
	se->depth = se->parent ? se->parent->depth + 1 : 0;
	
	if (!queued) {
		cfs_rq = cfs_rq_of(se);
		se->vruntime += cfs_rq->min_vruntime;
#ifdef CONFIG_SMP
		/*
		 * migrate_task_rq_fair() will have removed our previous
		 * contribution, but we must synchronize for ongoing future
		 * decay.
		 */
		se->avg.decay_count = atomic64_read(&cfs_rq->decay_counter);
		cfs_rq->blocked_load_avg += se->avg.load_avg_contrib;
#endif
	}
}

/*********************************************************************************************************
** 函数名称: free_fair_sched_group
** 功能描述: 在 cfs 调度类中用来释放指定的任务组
** 输	 入: tg - 指定的任务组指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void free_fair_sched_group(struct task_group *tg)
{
	int i;

    /* 尝试销毁指定的 cfs 带宽控制结构 */
	destroy_cfs_bandwidth(tg_cfs_bandwidth(tg));

    /* 释放指定任务组在每个 cpu 上的 cfs 运行队列结构和调度实例结构 */
	for_each_possible_cpu(i) {
		if (tg->cfs_rq)
			kfree(tg->cfs_rq[i]);
		if (tg->se)
			kfree(tg->se[i]);
	}

	kfree(tg->cfs_rq);
	kfree(tg->se);
}

/*********************************************************************************************************
** 函数名称: alloc_fair_sched_group
** 功能描述: 在 cfs 调度类中用来初始化指定的任务组结构
** 输	 入: tg - 指定的任务组指针
**         : parent - 指定的任务组父节点指针
** 输	 出: 1 - 初始化成功
**         : 0 - 初始化失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se;
	int i;

	/* 为指定的任务组在每个 cpu 上分配一个 cfs 运行队列实例指针数组结构 */
	tg->cfs_rq = kzalloc(sizeof(cfs_rq) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->cfs_rq)
		goto err;

	/* 为指定的任务组在每个 cpu 上分配一个调度实例指针数组结构 */
	tg->se = kzalloc(sizeof(se) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->se)
		goto err;

	tg->shares = NICE_0_LOAD;

    /* 初始化指定的 cfs 带宽控制结构 */
	init_cfs_bandwidth(tg_cfs_bandwidth(tg));

    /* 为指定的任务组在每个 cpu 上分配并初始化 cfs 运行队列结构和调度实例结构 */
	for_each_possible_cpu(i) {
		cfs_rq = kzalloc_node(sizeof(struct cfs_rq),
				      GFP_KERNEL, cpu_to_node(i));
		if (!cfs_rq)
			goto err;

		se = kzalloc_node(sizeof(struct sched_entity),
				  GFP_KERNEL, cpu_to_node(i));
		if (!se)
			goto err_free_rq;

		init_cfs_rq(cfs_rq);
		init_tg_cfs_entry(tg, cfs_rq, se, i, parent->se[i]);
	}

	return 1;

err_free_rq:
	kfree(cfs_rq);
err:
	return 0;
}

/*********************************************************************************************************
** 函数名称: unregister_fair_sched_group
** 功能描述: 在 cfs 调度类中用来把指定 cpu 上指定任务组的 cfs 运行队列从所属 cpu 运行队列上移除
** 输	 入: tg - 指定的任务组指针
**         : cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void unregister_fair_sched_group(struct task_group *tg, int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags;

	/*
	* Only empty task groups can be destroyed; so we can speculatively
	* check on_list without danger of it being re-added.
	*/
	if (!tg->cfs_rq[cpu]->on_list)
		return;

	raw_spin_lock_irqsave(&rq->lock, flags);
	list_del_leaf_cfs_rq(tg->cfs_rq[cpu]);
	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*********************************************************************************************************
** 函数名称: init_tg_cfs_entry
** 功能描述: 根据函数指定的参数初始化指定的 cfs 任务组实例结构
** 输	 入: tg - 指定的任务组实例指针
**         : cfs_rq- 为指定任务组分配的 cfs 运行队列指针
**         : se - 表示和指定任务组对应的调度实例指针
**         : cpu - 为指定任务组分配的 cpu id
**         : parent - 为指定的任务组实例分配的父任务组节点实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void init_tg_cfs_entry(struct task_group *tg, struct cfs_rq *cfs_rq,
			struct sched_entity *se, int cpu,
			struct sched_entity *parent)
{
	struct rq *rq = cpu_rq(cpu);

	cfs_rq->tg = tg;
	cfs_rq->rq = rq;

	/* 初始化指定的 cfs 运行队列中和带宽控制运行时时间相关的成员 */
	init_cfs_rq_runtime(cfs_rq);

	tg->cfs_rq[cpu] = cfs_rq;
	tg->se[cpu] = se;

	/* se could be NULL for root_task_group */
	if (!se)
		return;

    /* 如果指定的 parent 指针变量为 NULL，则表示当前调度任务组实例直接挂在指定的
	   cpu 运行队列中的 cfs 运行队列上，即当前任务组实例是任务组树的根节点 */
	if (!parent) {
		se->cfs_rq = &rq->cfs;
		se->depth = 0;
	} else {
		se->cfs_rq = parent->my_q;
		se->depth = parent->depth + 1;
	}

	se->my_q = cfs_rq;
	/* guarantee group entities always have weight */
	update_load_set(&se->load, NICE_0_LOAD);
	se->parent = parent;
}

static DEFINE_MUTEX(shares_mutex);

/*********************************************************************************************************
** 函数名称: sched_group_set_shares
** 功能描述: 设置指定任务组的 shares 字段值为指定的 shares 值
** 输	 入: tg - 指定的任务组实例指针
**         : shares - 指定的 shares 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int sched_group_set_shares(struct task_group *tg, unsigned long shares)
{
	int i;
	unsigned long flags;

	/*
	 * We can't change the weight of the root cgroup.
	 */
	if (!tg->se[0])
		return -EINVAL;

	shares = clamp(shares, scale_load(MIN_SHARES), scale_load(MAX_SHARES));

	mutex_lock(&shares_mutex);
	if (tg->shares == shares)
		goto done;

	tg->shares = shares;
	for_each_possible_cpu(i) {
		struct rq *rq = cpu_rq(i);
		struct sched_entity *se;

		se = tg->se[i];
		/* Propagate contribution to hierarchy */
		raw_spin_lock_irqsave(&rq->lock, flags);

		/* Possible calls to update_curr() need rq clock */
		update_rq_clock(rq);
		for_each_sched_entity(se)
			update_cfs_shares(group_cfs_rq(se));
		raw_spin_unlock_irqrestore(&rq->lock, flags);
	}

done:
	mutex_unlock(&shares_mutex);
	return 0;
}
#else /* CONFIG_FAIR_GROUP_SCHED */

/*********************************************************************************************************
** 函数名称: free_fair_sched_group
** 功能描述: 在 cfs 调度类中用来释放指定的任务组
** 输	 入: tg - 指定的任务组指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void free_fair_sched_group(struct task_group *tg) { }

/*********************************************************************************************************
** 函数名称: alloc_fair_sched_group
** 功能描述: 在 cfs 调度类中用来初始化指定的任务组结构
** 输	 入: tg - 指定的任务组指针
**         : parent - 指定的任务组父节点指针
** 输	 出: 1 - 初始化成功
**         : 0 - 初始化失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent)
{
	return 1;
}

/*********************************************************************************************************
** 函数名称: unregister_fair_sched_group
** 功能描述: 在 cfs 调度类中用来把指定 cpu 上指定任务组的 cfs 运行队列从所属 cpu 运行队列上移除
** 输	 入: tg - 指定的任务组指针
**         : cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void unregister_fair_sched_group(struct task_group *tg, int cpu) { }

#endif /* CONFIG_FAIR_GROUP_SCHED */

/*********************************************************************************************************
** 函数名称: get_rr_interval_fair
** 功能描述: 获取指定的任务实例在指定 cpu 运行队列上可以分配到的 round robin 时间片
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : task - 指定的任务指针
** 输	 出: rr_interval - 分配到的 round robin 时间片，单位是一个 HZ 周期
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static unsigned int get_rr_interval_fair(struct rq *rq, struct task_struct *task)
{
	struct sched_entity *se = &task->se;
	unsigned int rr_interval = 0;

	/*
	 * Time slice is 0 for SCHED_OTHER tasks that are on an otherwise
	 * idle runqueue:
	 */
	if (rq->cfs.load.weight)
		rr_interval = NS_TO_JIFFIES(sched_slice(cfs_rq_of(se), se));

	return rr_interval;
}

/*
 * All the scheduling class methods:
 */
const struct sched_class fair_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,
	.yield_to_task		= yield_to_task_fair,

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_fair,
	.migrate_task_rq	= migrate_task_rq_fair,

	.rq_online		= rq_online_fair,
	.rq_offline		= rq_offline_fair,

	.task_waking		= task_waking_fair,
#endif

	.set_curr_task          = set_curr_task_fair,
	.task_tick		= task_tick_fair,
	.task_fork		= task_fork_fair,

	.prio_changed		= prio_changed_fair,
	.switched_from		= switched_from_fair,
	.switched_to		= switched_to_fair,

	.get_rr_interval	= get_rr_interval_fair,

	.update_curr		= update_curr_fair,

#ifdef CONFIG_FAIR_GROUP_SCHED
	.task_move_group	= task_move_group_fair,
#endif
};

#ifdef CONFIG_SCHED_DEBUG
/*********************************************************************************************************
** 函数名称: print_cfs_stats
** 功能描述: 打印指定的 cpu 上的 cfs 运行队列信息
** 输	 入: m - 指定的 seq_file 文件指针
**         : cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void print_cfs_stats(struct seq_file *m, int cpu)
{
	struct cfs_rq *cfs_rq;

	rcu_read_lock();
	for_each_leaf_cfs_rq(cpu_rq(cpu), cfs_rq)
		print_cfs_rq(m, cpu, cfs_rq);
	rcu_read_unlock();
}
#endif

/*********************************************************************************************************
** 函数名称: init_sched_fair_class
** 功能描述: 初始化 cfs 调度类
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
__init void init_sched_fair_class(void)
{
#ifdef CONFIG_SMP
	open_softirq(SCHED_SOFTIRQ, run_rebalance_domains);

#ifdef CONFIG_NO_HZ_COMMON
	nohz.next_balance = jiffies;
	zalloc_cpumask_var(&nohz.idle_cpus_mask, GFP_NOWAIT);
	cpu_notifier(sched_ilb_notifier, 0);
#endif
#endif /* SMP */

}
