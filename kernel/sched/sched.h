
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/sched/deadline.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/stop_machine.h>
#include <linux/tick.h>
#include <linux/slab.h>

#include "cpupri.h"
#include "cpudeadline.h"
#include "cpuacct.h"

struct rq;
struct cpuidle_state;

/* task_struct::on_rq states: */
/* 表示当前调度实例在所属运行队列上，详情见 attach_task 函数 */
#define TASK_ON_RQ_QUEUED	1

/* 表示当前调度实例正在执行任务迁移且在所属负载均衡环境的链表上
   详情见 detach_task 和 detach_tasks 函数 */
#define TASK_ON_RQ_MIGRATING	2  

extern __read_mostly int scheduler_running;

extern unsigned long calc_load_update;
extern atomic_long_t calc_load_tasks;

extern long calc_load_fold_active(struct rq *this_rq);
extern void update_cpu_load_active(struct rq *this_rq);

/*
 * Helpers for converting nanosecond timing to jiffy resolution
 */
#define NS_TO_JIFFIES(TIME)	((unsigned long)(TIME) / (NSEC_PER_SEC / HZ))

/*
 * Increase resolution of nice-level calculations for 64-bit architectures.
 * The extra resolution improves shares distribution and load balancing of
 * low-weight task groups (eg. nice +19 on an autogroup), deeper taskgroup
 * hierarchies, especially on larger systems. This is not a user-visible change
 * and does not change the user-interface for setting shares/weights.
 *
 * We increase resolution only if we have enough bits to allow this increased
 * resolution (i.e. BITS_PER_LONG > 32). The costs for increasing resolution
 * when BITS_PER_LONG <= 32 are pretty high and the returns do not justify the
 * increased costs.
 */
/* 获取和指定的负载权重对应的值 */
#if 0 /* BITS_PER_LONG > 32 -- currently broken: it increases power usage under light load  */
# define SCHED_LOAD_RESOLUTION	10
# define scale_load(w)		((w) << SCHED_LOAD_RESOLUTION)
# define scale_load_down(w)	((w) >> SCHED_LOAD_RESOLUTION)
#else
# define SCHED_LOAD_RESOLUTION	0
# define scale_load(w)		(w)
# define scale_load_down(w)	(w)
#endif

#define SCHED_LOAD_SHIFT	(10 + SCHED_LOAD_RESOLUTION)  /*       10 */

/* 表示单个 cpu 的最大 cpu capacity 值 */
#define SCHED_LOAD_SCALE	(1L << SCHED_LOAD_SHIFT)      /* 1L << 10 */

#define NICE_0_LOAD		SCHED_LOAD_SCALE                  /* 1L << 10 */
#define NICE_0_SHIFT		SCHED_LOAD_SHIFT              /*       10 */

/*
 * Single value that decides SCHED_DEADLINE internal math precision.
 * 10 -> just above 1us
 * 9  -> just above 0.5us
 */
#define DL_SCALE (10)

/*
 * These are the 'tuning knobs' of the scheduler:
 */

/*
 * single value that denotes runtime == period, ie unlimited time.
 */
#define RUNTIME_INF	((u64)~0ULL)

/*********************************************************************************************************
** 函数名称: fair_policy
** 功能描述: 判断指定的调度策略是否为完全公平调度策略
** 输	 入: policy - 指定的调度策略
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int fair_policy(int policy)
{
	return policy == SCHED_NORMAL || policy == SCHED_BATCH;
}

/*********************************************************************************************************
** 函数名称: rt_policy
** 功能描述: 判断指定的调度策略是否为实时调度策略
** 输	 入: policy - 指定的调度策略
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int rt_policy(int policy)
{
	return policy == SCHED_FIFO || policy == SCHED_RR;
}

/*********************************************************************************************************
** 函数名称: dl_policy
** 功能描述: 判断指定的调度策略是否为 DEADLINE 调度策略
** 输	 入: policy - 指定的调度策略
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int dl_policy(int policy)
{
	return policy == SCHED_DEADLINE;
}

/*********************************************************************************************************
** 函数名称: task_has_rt_policy
** 功能描述: 判断指定的任务的调度策略是否为实时调度策略
** 输	 入: p - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_has_rt_policy(struct task_struct *p)
{
	return rt_policy(p->policy);
}

/*********************************************************************************************************
** 函数名称: task_has_dl_policy
** 功能描述: 判断指定的任务的调度策略是否为 DEADLINE 调度策略
** 输	 入: p - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_has_dl_policy(struct task_struct *p)
{
	return dl_policy(p->policy);
}

static inline bool dl_time_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

/*
 * Tells if entity @a should preempt entity @b.
 */
static inline bool
dl_entity_preempt(struct sched_dl_entity *a, struct sched_dl_entity *b)
{
	return dl_time_before(a->deadline, b->deadline);
}

/*
 * This is the priority-queue data structure of the RT scheduling class:
 */
struct rt_prio_array {
	DECLARE_BITMAP(bitmap, MAX_RT_PRIO+1); /* include 1 bit for delimiter */
	struct list_head queue[MAX_RT_PRIO];
};

struct rt_bandwidth {
	/* nests inside the rq lock: */
	raw_spinlock_t		rt_runtime_lock;
	ktime_t			rt_period;
	u64			rt_runtime;
	struct hrtimer		rt_period_timer;
};

void __dl_clear_params(struct task_struct *p);

/*
 * To keep the bandwidth of -deadline tasks and groups under control
 * we need some place where:
 *  - store the maximum -deadline bandwidth of the system (the group);
 *  - cache the fraction of that bandwidth that is currently allocated.
 *
 * This is all done in the data structure below. It is similar to the
 * one used for RT-throttling (rt_bandwidth), with the main difference
 * that, since here we are only interested in admission control, we
 * do not decrease any runtime while the group "executes", neither we
 * need a timer to replenish it.
 *
 * With respect to SMP, the bandwidth is given on a per-CPU basis,
 * meaning that:
 *  - dl_bw (< 100%) is the bandwidth of the system (group) on each CPU;
 *  - dl_total_bw array contains, in the i-eth element, the currently
 *    allocated bandwidth on the i-eth CPU.
 * Moreover, groups consume bandwidth on each CPU, while tasks only
 * consume bandwidth on the CPU they're running on.
 * Finally, dl_total_bw_cpu is used to cache the index of dl_total_bw
 * that will be shown the next time the proc or cgroup controls will
 * be red. It on its turn can be changed by writing on its own
 * control.
 */
struct dl_bandwidth {
	raw_spinlock_t dl_runtime_lock;
	u64 dl_runtime;
	u64 dl_period;
};

/*********************************************************************************************************
** 函数名称: dl_bandwidth_enabled
** 功能描述: 判断当前系统是否使能了 deadline 带宽控制功能
** 输	 入: 
** 输	 出: 1 - 使能了
**         : 0 - 没使能
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int dl_bandwidth_enabled(void)
{
	return sysctl_sched_rt_runtime >= 0;
}

extern struct dl_bw *dl_bw_of(int i);

/* 定义了保存 deadline 带宽控制相关参数数据结构 */
struct dl_bw {
	raw_spinlock_t lock;

	/* bw - 表示当前系统“每个” cpu 可提供的最大带宽，-1 表示无效的参数
	   total_bw - 表示当前系统“所有” cpu 一共使用的带宽 */
	u64 bw, total_bw;
};

/*********************************************************************************************************
** 函数名称: __dl_clear
** 功能描述: 在我们移除一个实时任务时调用，用来更新当前系统 deadline 带宽使用量
** 输	 入: dl_b - 指定的 deadline 带宽控制结构指针
**         : tsk_bw - 移除任务的带宽使用量
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline
void __dl_clear(struct dl_bw *dl_b, u64 tsk_bw)
{
	dl_b->total_bw -= tsk_bw;
}

/*********************************************************************************************************
** 函数名称: __dl_add
** 功能描述: 在我们添加一个实时任务时调用，用来更新当前系统 deadline 带宽使用量
** 输	 入: dl_b - 指定的 deadline 带宽控制结构指针
**         : tsk_bw - 添加任务的带宽使用量
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline
void __dl_add(struct dl_bw *dl_b, u64 tsk_bw)
{
	dl_b->total_bw += tsk_bw;
}

/*********************************************************************************************************
** 函数名称: __dl_overflow
** 功能描述: 在系统中实时任务带宽使用量变化时调用，用来判断变化之后实时任务的总带宽使用量是否
**         : 超过了我们预先为实时任务分配的阈值
** 输	 入: dl_b - 指定的 deadline 带宽控制结构指针
**         : cpus - 指定的 cpu 所属根调度域中参与 deadline 带宽控制的有效 cpu 个数
**         : old_bw - 变化前的任务带宽使用量
**         : new_bw - 变化后的任务带宽使用量
** 输	 出: 1 - 超过了预先设定的阈值
**         : 0 - 没超过预先设定的阈值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline
bool __dl_overflow(struct dl_bw *dl_b, int cpus, u64 old_bw, u64 new_bw)
{
	return dl_b->bw != -1 &&
	       dl_b->bw * cpus < dl_b->total_bw - old_bw + new_bw;
}

extern struct mutex sched_domains_mutex;

#ifdef CONFIG_CGROUP_SCHED

#include <linux/cgroup.h>

struct cfs_rq;
struct rt_rq;

extern struct list_head task_groups;

/* 带宽控制基本工作原理：
   通过设置两个变量 quota 和 period，period 是指一段周期时间，quota 是指在 period 周期
   时间内，一个组可以使用的 cpu 时间限额。当一个组的进程运行时间超过 quota 后，就会被
   限制运行，这个动作被称作 throttle。直到下一个 period 周期开始，这个组会被重新调度
   这个过程称作 unthrottle */
struct cfs_bandwidth {
#ifdef CONFIG_CFS_BANDWIDTH
	raw_spinlock_t lock;

    /* 表示当前带宽控制池所属任务组在带宽控制时使用的统计周期 */
	ktime_t period;
	
    /* quota - 表示在指定的周期内为当前带宽控制池分配的时间额度，RUNTIME_INF 不表示不限制带宽
       runtime - 表示在指定的周期内当前带宽控制池剩余的可运行时间额度 */
	u64 quota, runtime;

	/* 表示当前带宽控制池的时间额度占其统计周期的比例值（乘以了精度因子），详情见 tg_cfs_schedulable_down 函数 */
	s64 hierarchical_quota;

	/* 表示当前带宽控制池所属任务组当前统计周期的到期时间的运行队列时钟，单位是 ns
	   详情见 __refill_cfs_bandwidth_runtime 函数 */
	u64 runtime_expires;

    /* idle - 表示当前带宽控制池的 throttled_cfs_rq 链表是否为空，详情见 do_sched_cfs_period_timer 函数
	   timer_active - 表示当前带宽控制池所属任务组使用的高精度定时器是否处于激活状态
	   详情见 __start_cfs_bandwidth 函数 */
	int idle, timer_active;

	/* period_timer - 表示当前带宽控制池所属任务组使用的周期超时高精度定时器
	   slack_timer - 表示当前带宽控制池的 slack 定时器，实现了尝试把已经分配给 cfs 运行队列的运行
	   时间拿回到当前带宽控制池中并分配给其他 cfs 运行队列的功能，详情见 __return_cfs_rq_runtime 和
	   do_sched_cfs_slack_timer 函数 */
	struct hrtimer period_timer, slack_timer;

    /* 把所有已经 throttle cfs 运行队列通过链表连接在一起，在 unthrottle 的时候使用
       详情见 distribute_cfs_runtime 和 unthrottle_cfs_rq 函数 */
	struct list_head throttled_cfs_rq;

	/* statistics */
	/* nr_periods - 表示当前带宽控制池已经运行的统计周期个数，详情见 do_sched_cfs_period_timer 函数
	   nr_throttled - 表示当前带宽控制池处于 throttled 状态共经历的周期数，详情见 do_sched_cfs_period_timer 函数 */
	int nr_periods, nr_throttled;

	/* 表示当前带宽控制池所属任务组在 throttled 状态下一共经历的时间，详情见 unthrottle_cfs_rq 函数 */
	u64 throttled_time;
#endif
};

/* task group related information */
/* 1. 设计调度组的原因？
      因为当前系统支持多用户功能，为了在多个用户之间均匀分配系统 cpu 资源
      我们把一个用户的所有进程放到同一个调度组内，然后以调度组为资源分配
      单位，并且给调度组分配不同的权重信息，这样就可以在多个用户之间灵活
      分配系统资源了 
   2. 表示组调度中的任务组结构，调度器通过树形结构把所有任务组链接起来 */
struct task_group {
	struct cgroup_subsys_state css;

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* schedulable entities of this group on each cpu */
    /* 这是一个二维数组指针，表示的是当前任务组在每一个 cpu 上的任务组实例结构指针，详情见 sched_init 函数 */
	struct sched_entity **se;

	/* runqueue "owned" by this group on each cpu */
    /* 这是一个二维数组指针，表示的是当前任务组在每一个 cpu 上拥有的 cfs 运行队列指针，详情见 sched_init 函数 */
	struct cfs_rq **cfs_rq;

	/* 表示当前任务组由其父节点看到的权重值，在计算这个任务组树的总权重时使用 */
	unsigned long shares;

#ifdef	CONFIG_SMP
	/* 表示当前任务组在过去“时间段”内经过衰减后的负载贡献值
	   这个负载贡献统计值的更新可能具有延迟性，详情见 __update_cfs_rq_tg_load_contrib 函数 */
	atomic_long_t load_avg;

    /* 表示当前任务组在指定的“统计周期”内没经过衰减的负载贡献值 */
	/*									   sa->runnable_avg_sum << NICE_0_SHIFT
	 tg->runnable_avg = tg->runnable_avg + ------------------------------------ - cfs_rq->tg_runnable_contrib
											   sa->runnable_avg_period + 1 
									
										   sa->runnable_avg_sum_new << NICE_0_SHIFT   sa->runnable_avg_sum_old << NICE_0_SHIFT
					  = tg->runnable_avg + ---------------------------------------- - ----------------------------------------						  
											   sa->runnable_avg_period_new + 1			  sa->runnable_avg_period_old + 1	*/
	atomic_t runnable_avg;
#endif
#endif

#ifdef CONFIG_RT_GROUP_SCHED
    /* 详情见 sched_init 函数 */
	struct sched_rt_entity **rt_se;

    /* 详情见 sched_init 函数 */
	struct rt_rq **rt_rq;

	struct rt_bandwidth rt_bandwidth;
#endif

	struct rcu_head rcu;
	struct list_head list;

    /* 指向当前任务组的父任务组节点指针 */
	struct task_group *parent;

    /* 通过链表的方式把当前任务组的所有兄弟任务组节点链接起来 */
	struct list_head siblings;
	
    /* 通过链表的方式把当前任务组的所有子任务组节点链接起来 */
	struct list_head children;

#ifdef CONFIG_SCHED_AUTOGROUP
	struct autogroup *autogroup;
#endif

    /* 表示当前任务组的带宽控制数据结构 */
	struct cfs_bandwidth cfs_bandwidth;
};

#ifdef CONFIG_FAIR_GROUP_SCHED
#define ROOT_TASK_GROUP_LOAD	NICE_0_LOAD

/*
 * A weight of 0 or 1 can cause arithmetics problems.
 * A weight of a cfs_rq is the sum of weights of which entities
 * are queued on this cfs_rq, so a weight of a entity should not be
 * too large, so as the shares value of a task group.
 * (The default weight is 1024 - so there's no practical
 *  limitation from this.)
 */
#define MIN_SHARES	(1UL <<  1)
#define MAX_SHARES	(1UL << 18)
#endif

typedef int (*tg_visitor)(struct task_group *, void *);

extern int walk_tg_tree_from(struct task_group *from,
			     tg_visitor down, tg_visitor up, void *data);

/*
 * Iterate the full tree, calling @down when first entering a node and @up when
 * leaving it for the final time.
 *
 * Caller must hold rcu_lock or sufficient equivalent.
 */
static inline int walk_tg_tree(tg_visitor down, tg_visitor up, void *data)
{
	return walk_tg_tree_from(&root_task_group, down, up, data);
}

extern int tg_nop(struct task_group *tg, void *data);

extern void free_fair_sched_group(struct task_group *tg);
extern int alloc_fair_sched_group(struct task_group *tg, struct task_group *parent);
extern void unregister_fair_sched_group(struct task_group *tg, int cpu);
extern void init_tg_cfs_entry(struct task_group *tg, struct cfs_rq *cfs_rq,
			struct sched_entity *se, int cpu,
			struct sched_entity *parent);
extern void init_cfs_bandwidth(struct cfs_bandwidth *cfs_b);
extern int sched_group_set_shares(struct task_group *tg, unsigned long shares);

extern void __refill_cfs_bandwidth_runtime(struct cfs_bandwidth *cfs_b);
extern void __start_cfs_bandwidth(struct cfs_bandwidth *cfs_b, bool force);
extern void unthrottle_cfs_rq(struct cfs_rq *cfs_rq);

extern void free_rt_sched_group(struct task_group *tg);
extern int alloc_rt_sched_group(struct task_group *tg, struct task_group *parent);
extern void init_tg_rt_entry(struct task_group *tg, struct rt_rq *rt_rq,
		struct sched_rt_entity *rt_se, int cpu,
		struct sched_rt_entity *parent);

extern struct task_group *sched_create_group(struct task_group *parent);
extern void sched_online_group(struct task_group *tg,
			       struct task_group *parent);
extern void sched_destroy_group(struct task_group *tg);
extern void sched_offline_group(struct task_group *tg);

extern void sched_move_task(struct task_struct *tsk);

#ifdef CONFIG_FAIR_GROUP_SCHED
extern int sched_group_set_shares(struct task_group *tg, unsigned long shares);
#endif

#else /* CONFIG_CGROUP_SCHED */

struct cfs_bandwidth { };

#endif	/* CONFIG_CGROUP_SCHED */

/* CFS-related fields in a runqueue */
struct cfs_rq {
    /* 表示当前 cfs 运行队列上所有调度实例的调度负载权重的总和，即把当前运行队列当成一个调度实例
       的时候，这个运行队列拥有的调度负载权重信息 */
	struct load_weight load;

	/* nr_running - 表示当前 cfs 运行队列上包含的调度实例个数
	   h_nr_running -  表示当前 cfs 运行队列以及所有子节点（任务组）上一共包含的调度实例个数 */
	unsigned int nr_running, h_nr_running;

	u64 exec_clock;

	/* 记录了当前 cfs 运行队列的调度实例中虚拟运行时间最小的值，即红黑树上最左边位置的调度实例 */
	u64 min_vruntime;
	
#ifndef CONFIG_64BIT
    /* 表示当前 cfs 运行队列的 min_vruntime 字段值的一份拷贝 */
	u64 min_vruntime_copy;
#endif

    /* 红黑树根节点，通过红黑树的方式把属于当前 cfs 运行队列的所有调度实例以虚拟运行时间为键值组织起来 */
	struct rb_root tasks_timeline;

    /* 表示在当前 cfs 运行队列的红黑树上剩余运行时间最多的调度实例节点，即下一次需要运行的调度实例 */
	struct rb_node *rb_leftmost;

	/*
	 * 'curr' points to currently running entity on this cfs_rq.
	 * It is set to NULL otherwise (i.e when none are currently running).
	 */
	/* curr - 指向了当前 cfs 运行队列中正在运行的调度实例指针，这个调度实例不在 cfs 运行队列的红黑树上
	   next - 详情见 features.h 文件
	   last - 详情见 features.h 文件
	   skip - 指向了当前 cfs 运行队列中调度时需要跳过的调度实例指针，详情见 yield_task_fair 函数 */
	struct sched_entity *curr, *next, *last, *skip;

#ifdef	CONFIG_SCHED_DEBUG
	unsigned int nr_spread_over;
#endif

#ifdef CONFIG_SMP
	/*
	 * CFS Load tracking
	 * Under CFS, load is tracked on a per-entity basis and aggregated up.
	 * This allows for the description of both thread and group usage (in
	 * the FAIR_GROUP_SCHED case).
	 */
	/* runnable_load_avg - 表示当前 cfs 运行队列上所有调度实例在指定的“时间段”内
	   经过衰减后的处于可运行状态（运行态）时间的平均负载贡献值，计算公式如下：
	                       se->avg.runnable_avg_sum * se->load.weight
	   runnable_load_avg = ------------------------------------------
	                            se->avg.runnable_avg_period + 1 
	   详情见 update_entity_load_avg 函数
	   
	   blocked_load_avg  - 表示当前 cfs 运行队列上所有调度实例在指定的“时间段”内
	   经过衰减后的处于被阻塞状态（睡眠态）时间的平均负载贡献值，计算公式如下：
	                      se->avg.runnable_avg_sum * se->load.weight
	   blocked_load_avg = ------------------------------------------
	                           se->avg.runnable_avg_period + 1 
	   详情见 update_entity_load_avg 函数以及 subtract_blocked_load_contrib 函数 */
	unsigned long runnable_load_avg, blocked_load_avg;

    /* 表示当前 cfs 运行队列对属于它的所有任务执行的负载贡献衰减阶数
       详情见 update_cfs_rq_blocked_load 函数 */
	atomic64_t decay_counter;

	/* 表是当前 cfs 运行队列上一次对负载执行衰减操作时的运行队列时钟，单位是 ms
	   详情见 update_cfs_rq_blocked_load 函数 */
	u64 last_decay;

	/* 表示从当前 cfs 运行队列中迁移到其他运行队列中的任务的负载贡献值总和
	   详情见 migrate_task_rq_fair 函数 */
	atomic_long_t removed_load;

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* Required to track per-cpu representation of a task_group */
    /* 表示当前 cfs 运行队列所属任务组在指定的“统计周期”内没经过衰减的负载贡献值 

                                     sched_avg->runnable_avg_sum << NICE_0_SHIFT
       cfs_rq->tg_runnable_contrib = -------------------------------------------
	                                     sched_avg->runnable_avg_period + 1   */
	u32 tg_runnable_contrib;

	/* 表示当前 cfs 运行队列所属任务组在过去“时间段”内经过衰减后的负载贡献值
	   这个负载贡献统计值的更新可能具有延迟性，详情见 __update_cfs_rq_tg_load_contrib 函数 */
	unsigned long tg_load_contrib;

	/*
	 * h_load = weight * f(tg)
	 *
	 * Where f(tg) is the recursive weight fraction assigned to this group.
	 */
    /* h_load 表示当前 cfs 运行队列所属任务组的负载贡献统计值对其父任务组节点的负载贡献量
       计算公式如下：
                              parent_cfs_rq->h_load * child_se->avg.load_avg_contrib
	   child_cfs_rq->h_load = ------------------------------------------------------
		                              parent_cfs_rq->runnable_load_avg + 1
		                              
	   当 parent_cfs_rq 为任务组树形结构的根节点时，parent_cfs_rq->h_load = 1，所以有如下公式：
	   
	                             child_se->avg.load_avg_contrib
	   child_cfs_rq->h_load = ------------------------------------
		                      parent_cfs_rq->runnable_load_avg + 1
		                      
	   详情见 update_cfs_rq_h_load 函数 */
	unsigned long h_load;

	/* 上次更新当前任务组的 h_load 数据时的系统时间，详情见 update_cfs_rq_h_load 函数 */
	u64 last_h_load_update;

    /* 表是下一个需要更新负载贡献值的调度实例指针，详情见 update_cfs_rq_h_load 函数 */
	struct sched_entity *h_load_next;
#endif /* CONFIG_FAIR_GROUP_SCHED */
#endif /* CONFIG_SMP */

#ifdef CONFIG_FAIR_GROUP_SCHED
    /* 指向当前 cfs 运行队列所属的 cpu 运行队列 */
	struct rq *rq;	/* cpu runqueue to which this cfs_rq is attached */

	/*
	 * leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
	 * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
	 * (like users, containers etc.)
	 *
	 * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a cpu. This
	 * list is used during load balance.
	 */
	/* 在当前运行队列属于某个任务组的时候，表示当前运行队列是否已经添加到了所属的 cpu 运行队列上 */
	int on_list;

	/* 通过这个链表节点把当前 cfs 运行队列添加到所属的 cpu 运行队列上，详情见 enqueue_entity 函数 */
	struct list_head leaf_cfs_rq_list;

	/* 在当前运行队列属于某个任务组的时候，表示当前运行队列所属的任务组 */
	struct task_group *tg;	/* group that "owns" this runqueue */

#ifdef CONFIG_CFS_BANDWIDTH
    /* 表示是否使能当前 cfs 运行队列的带宽控制功能，详情见 update_runtime_enabled 函数 */
	int runtime_enabled;

    /* 表示当前 cfs 运行队列带宽控制的当前统计周期的到期时间 */
	u64 runtime_expires;

	/* 表示当前 cfs 运行队列还剩余的可运行时间，主要在带宽控制时使用 */
	s64 runtime_remaining;

    /* throttled_clock - 表是当前 cfs 运行队列上一次执行 throttled 操作时所属 cpu 运行队列的时钟值
	   throttled_clock_task - 表示当前 cfs 运行队列第一次执行 throttled 操作时的所属 cpu 运行队列的任务时钟值 */
	u64 throttled_clock, throttled_clock_task;

	/* 表示当前 cfs 运行队列在 throttled 状态下一共经历的时间长度，详情见 tg_unthrottle_up 函数 */
	u64 throttled_clock_task_time;

	/* throttled - 表示当前 cfs 运行队列是否 throttled
	   throttle_count - 表示当前 cfs 运行队列执行 throttled 操作的次数，在 hierarchy throttled 时会累加 */
	int throttled, throttle_count;

	/* 在对当前 cfs 运行队列执行 throttled 操作时把当前队列添加到所属任务组的带宽控制结构的链表中
	   在 unthrottled 的时候使用，详情见 unthrottle_cfs_rq 函数 */
	struct list_head throttled_list;
#endif /* CONFIG_CFS_BANDWIDTH */
#endif /* CONFIG_FAIR_GROUP_SCHED */
};

static inline int rt_bandwidth_enabled(void)
{
	return sysctl_sched_rt_runtime >= 0;
}

/* Real-Time classes' related field in a runqueue: */
struct rt_rq {
	struct rt_prio_array active;
	unsigned int rt_nr_running;
#if defined CONFIG_SMP || defined CONFIG_RT_GROUP_SCHED
	struct {
		int curr; /* highest queued rt task prio */
#ifdef CONFIG_SMP
		int next; /* next highest */
#endif
	} highest_prio;
#endif
#ifdef CONFIG_SMP
	unsigned long rt_nr_migratory;
	unsigned long rt_nr_total;
	int overloaded;
	struct plist_head pushable_tasks;
#endif
	int rt_queued;

	int rt_throttled;
	u64 rt_time;
	u64 rt_runtime;
	/* Nests inside the rq lock: */
	raw_spinlock_t rt_runtime_lock;

#ifdef CONFIG_RT_GROUP_SCHED
	unsigned long rt_nr_boosted;

	struct rq *rq;
	struct task_group *tg;
#endif
};

/* Deadline class' related fields in a runqueue */
struct dl_rq {
	/* runqueue is an rbtree, ordered by deadline */
	struct rb_root rb_root;
	struct rb_node *rb_leftmost;

	unsigned long dl_nr_running;

#ifdef CONFIG_SMP
	/*
	 * Deadline values of the currently executing and the
	 * earliest ready task on this rq. Caching these facilitates
	 * the decision wether or not a ready but not running task
	 * should migrate somewhere else.
	 */
	struct {
		u64 curr;
		u64 next;
	} earliest_dl;

	unsigned long dl_nr_migratory;
	int overloaded;

	/*
	 * Tasks on this rq that can be pushed away. They are kept in
	 * an rb-tree, ordered by tasks' deadlines, with caching
	 * of the leftmost (earliest deadline) element.
	 */
	struct rb_root pushable_dl_tasks_root;
	struct rb_node *pushable_dl_tasks_leftmost;
#else
	struct dl_bw dl_bw;
#endif
};

#ifdef CONFIG_SMP

/*
 * We add the notion of a root-domain which will be used to define per-domain
 * variables. Each exclusive cpuset essentially defines an island domain by
 * fully partitioning the member cpus from any other cpuset. Whenever a new
 * exclusive cpuset is created, we also create and attach a new root-domain
 * object.
 */
struct root_domain {
    /* 表示当前根调度域的引用计数 */
	atomic_t refcount;
	
	atomic_t rto_count;
	struct rcu_head rcu;

	/* 表示当前根调度域内包含的 cpu 的位图掩码值，详情见 rq_attach_root 函数 */
	cpumask_var_t span;

	/* 表示当前调度域内处于 online 状态的 cpu 运行队列位图掩码值，详情见 set_rq_online 函数 */
	cpumask_var_t online;

	/* Indicate more than one runnable task for any CPU */
	bool overload;

	/*
	 * The bit corresponding to a CPU gets set here if such CPU has more
	 * than one runnable -deadline task (as it is below for RT tasks).
	 */
	cpumask_var_t dlo_mask;
	atomic_t dlo_count;
	struct dl_bw dl_bw;
	struct cpudl cpudl;

	/*
	 * The "RT overload" flag: it gets set if a CPU has more than
	 * one runnable RT task.
	 */
	cpumask_var_t rto_mask;
	struct cpupri cpupri;
};

extern struct root_domain def_root_domain;

#endif /* CONFIG_SMP */

/*
 * This is the main, per-CPU runqueue data structure.
 *
 * Locking rule: those places that want to lock multiple runqueues
 * (such as the load balancing or the thread migration code), lock
 * acquire operations must be ordered by ascending &runqueue.
 */
struct rq {
	/* runqueue lock: */
	raw_spinlock_t lock;

	/*
	 * nr_running and cpu_load should be in the same cacheline because
	 * remote CPUs use both these fields when doing load calculation.
	 */

	/* 表示当前 cpu 运行队列上包含的实时调度实例和 cfs 调度实例数 */
	unsigned int nr_running;
	
#ifdef CONFIG_NUMA_BALANCING
    /* 表示当前 cpu 运行队列中包含的已经分配了 preferred_node 的调度实例数 */
	unsigned int nr_numa_running;

    /* 表示当前 cpu 运行队列中包含的 preferred_node == task_node 的调度实例数 */
	unsigned int nr_preferred_running;
#endif
    /* 在计算当前 cpu 运行队列的负载贡献时，如果只看某一时刻的负载值，是无法准确体会
       当前 cpu 运行队列的负载情况的，必须将一段时间内的负载值综合起来看才行。于是
       cpu 运行队列中维护了一个保存负载值的数组，他们分别表示在不同统计方式下，当前
       cpu 运行队列的负载贡献情况，详情见 __update_cpu_load 函数，这个数组在 smp 负载
       均衡时会使用到，用来计算是否需要执行负载均衡任务迁移操作，如果希望进行任务迁移
       那么应该选择较小的 i 值，因为此时的 cpu_load[i] 抖动比较大，容易发现不均衡。反
       之，如果希望保持稳定，那么应该选择较大的 i 值

       那么，什么时候倾向于进行迁移、什么时候又倾向于保持稳定呢？这要从两个维度来看：
       
       第一个维度，是当前 CPU 的状态。这里会考虑三种 CPU 状态：
       1、CPU 刚进入 IDLE（比如说 CPU 上唯一的 TASK_RUNNING 状态的进程睡眠去了），这时
          候是很渴望马上弄一个进程过来运行的，应该选择较小的 i 值
       2、CPU 处于 IDLE，这时候还是很渴望弄一个进程过来运行的，但是可能已经尝试过几次
          都无果了，故选择略大一点的 i 值
       3、CPU 非 IDLE，有进程正在运行，这时候就不太希望进程迁移了，会选择较大的 i 值

       第二个维度，是 CPU 的亲缘性。离得越近的 CPU，进程迁移所造成的缓存失效的影响越小
       应该选择较小的 i 值。比如两个 CPU 是同一物理 CPU 的同一核心通过 SMT（超线程技术）
       虚拟出来的，那么它们的缓存大部分是共享的。进程在它们之间迁移代价较小。反之则应该
       选择较大的 i 值，linux 是通过调度域来管理 CPU 的亲缘性的，通过调度域的描述，内核
       就可以知道 CPU 与 CPU 的亲缘关系。对于关系远的 CPU，尽量少在它们之间迁移进程，而
       对于关系近的 CPU，则可以容忍较多一些的进程迁移 */
	#define CPU_LOAD_IDX_MAX 5
	unsigned long cpu_load[CPU_LOAD_IDX_MAX];
	
	unsigned long last_load_update_tick;
#ifdef CONFIG_NO_HZ_COMMON
	u64 nohz_stamp;

    /* 在 idle load balance 中使用，详情见 nohz_balancer_kick 函数和 NOHZ_BALANCE_KICK 定义 */
	unsigned long nohz_flags;
#endif

#ifdef CONFIG_NO_HZ_FULL
    /* 用来追踪记录当前 cpu 运行队列最后一次 tick 的 jiffies 值，详情见 rq_last_tick_reset 函数 */
	unsigned long last_sched_tick;
#endif

	/* capture load from *all* tasks on this cpu: */
    /* 表示当前 cpu 运行队列上所有调度实例的调度负载权重的总和，即把当前 cpu 运行队列当成一个调度实例
       的时候，这个 cpu 运行队列拥有的调度负载权重信息 */	
	struct load_weight load;

	unsigned long nr_load_updates;

	/* 统计当前 cpu 运行队列上发生的任务切换次数，详情见 __schedule 函数 */
	u64 nr_switches;

	struct cfs_rq cfs;
	struct rt_rq rt;
	struct dl_rq dl;

#ifdef CONFIG_FAIR_GROUP_SCHED
	/* list of leaf cfs_rq on this cpu: */
    /* 通过链表的方式把当前 cpu 运行队列上的所有 cfs 运行队列链接起来，详情见 enqueue_entity 函数 */
	struct list_head leaf_cfs_rq_list;

	struct sched_avg avg;
#endif /* CONFIG_FAIR_GROUP_SCHED */

	/*
	 * This is part of a global counter where only the total sum
	 * over all CPUs matters. A task can increase this counter on
	 * one CPU and if it got migrated afterwards it may decrease
	 * it on another CPU. Always updated under the runqueue lock:
	 */
	/* 表示当前 cpu 运行队列上包含的处于不可被中断状态的实时调度实例和 cfs 调度实例数 */
	unsigned long nr_uninterruptible;

    /* curr - 表示当前 cpu 运行队列正在运行的任务指针 
	   idle - 表示当前 cpu 运行队列的 idle 任务指针
	   stop - 表示当前 cpu 运行队列的 stop 任务指针，stop 任务是系统中优先级最高的任务
	          它抢占所有的任务，并且不会被任何东西抢占 */
	struct task_struct *curr, *idle, *stop;

	/* 表示当前 cpu 运行队列下一次触发负载均衡的 jiffies 时间，详情见 rebalance_domains 函数
	   和 idle_balance 函数以及 trigger_load_balance 函数等 */
	unsigned long next_balance;

	/* 在当前 cpu 运行队列执行任务切换时用来保存被切换出任务的内存结构指针，详情见
	   context_switch 函数和 finish_task_switch 函数 */
	struct mm_struct *prev_mm;

	unsigned int clock_skip_update;

	/* 用来记录当前运行队列的基准时钟信息，在 update_rq_clock 函数中更新，单位是 ns */
	u64 clock;

    /* 表示当前 cpu 运行队列中的调度实例在任务上下文中消耗的时间，在 update_rq_clock_task 函数中更新，单位是 ns */
	u64 clock_task;

    /* 表示当前 cpu 运行队列上目前一共存在的 IO 等待任务个数，详情见 io_schedule_timeout 函数 */
	atomic_t nr_iowait;

#ifdef CONFIG_SMP
    /* 表示当前 cpu 运行队列所属根调度域指针，详情见 rq_attach_root 函数 */
	struct root_domain *rd;

    /* 表示当前 cpu 运行队列的调度域指针，详情见 cpu_attach_domain 函数 */
	struct sched_domain *sd;

	/* 表示当前 cpu 运行队列在减去实时调度实例运行的时间后，给 cfs 调度实例
	   剩余的负载能力运行时间，详情见 update_cpu_capacity 函数 */
	unsigned long cpu_capacity;

    /* 表示当前 cpu 运行队列是否处于 idle 状态，即是否可以执行 idle 负载均衡操作
       详情见 scheduler_tick 函数和 nohz_kick_needed 函数 */
	unsigned char idle_balance;
	
	/* For active balancing */
	int post_schedule;

	/* 详情见 load_balance 函数（设置 active_balance 标志）和
	   active_load_balance_cpu_stop 函数（清除 active_balance 标志）*/
	int active_balance;

	/* 详情见 active_load_balance_cpu_stop 函数 */
	int push_cpu;
	
	struct cpu_stop_work active_balance_work;

	/* cpu of this runqueue: */
	int cpu;

	/* 表示当前 cpu 运行队列状态，1 表示 online，0 表示 offline，详情见 set_rq_online 函数 */
	int online;

    /* 把属于当前 cpu 运行队列的所有任务通过链表链接起来 */
	struct list_head cfs_tasks;

    /* 表示当前 cpu 运行队列内实时调度实例在过去一段“时间内”经过减半衰减的运行时间统计信息
	   详情见 sched_rt_avg_update 函数和 sched_avg_update 函数 */
	u64 rt_avg;

	/* 表示当前 cpu 运行队列的调度时间轴信息，单位是 ns，更新粒度为 sched_avg_period
	   详情见 sched_avg_update 函数和 scale_rt_capacity 函数 */
	u64 age_stamp;

    /* 表示当前 cpu 运行队列在进入 idle 状态下开始执行负载均衡操作时的 cpu 运行队列时钟，详情见 idle_balance 函数 */
	u64 idle_stamp;
	
	u64 avg_idle;

	/* This is used to determine avg_idle's max value */
	/* 表示当前 cpu 运行队列在 idle 状态下从其他忙 cpu 上拉取任务时消耗的最大时间，单位是 ns，详情见 idle_balance 函数 */
	u64 max_idle_balance_cost;
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
    /* 表示当前 cpu 运行队列中的任务在中断上下文中消耗的时间，在 update_rq_clock_task 函数中更新 */
	u64 prev_irq_time;
#endif

/* 下面两个是和虚拟化相关的时间统计信息，在 update_rq_clock_task 函数中更新 */
#ifdef CONFIG_PARAVIRT
	u64 prev_steal_time;
#endif
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
	u64 prev_steal_time_rq;
#endif

	/* calc_load related fields */
    /* 单位是一个 tick 周期 */
	unsigned long calc_load_update;

    /* 表示当前 cpu 运行队列上次统计系统全局平均负载使用的实时调度实例和 cfs 调度实例数
       详情见 calc_load_fold_active 函数 */
	long calc_load_active;

#ifdef CONFIG_SCHED_HRTICK
#ifdef CONFIG_SMP
    /* 详情见 hrtick_start 函数 */
	int hrtick_csd_pending;
	struct call_single_data hrtick_csd;
#endif
    /* 表示当前 cpu 运行队列的高精度定时器，在超时处理函数中更新 cpu 运行队列时钟并触发调度类 tick */
	struct hrtimer hrtick_timer;
#endif

#ifdef CONFIG_SCHEDSTATS
	/* latency stats */
	struct sched_info rq_sched_info;
	unsigned long long rq_cpu_time;
	/* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

	/* sys_sched_yield() stats */
	unsigned int yld_count;

	/* schedule() stats */
	unsigned int sched_count;
	unsigned int sched_goidle;

	/* try_to_wake_up() stats */
	unsigned int ttwu_count;
	unsigned int ttwu_local;
#endif

#ifdef CONFIG_SMP
    /* 通过链表的方式把当前 cpu 运行队列中所有待唤醒的任务链接起来，详情见 sched_ttwu_pending 函数 */
	struct llist_head wake_list;
#endif

#ifdef CONFIG_CPU_IDLE
	/* Must be inspected within a rcu lock section */
	struct cpuidle_state *idle_state;
#endif
};

/*********************************************************************************************************
** 函数名称: cpu_of
** 功能描述: 获取指定的 cpu 运行队列所属的 cpu 号
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int cpu_of(struct rq *rq)
{
#ifdef CONFIG_SMP
	return rq->cpu;
#else
	return 0;
#endif
}

DECLARE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);

/* 获取指定 cpu 上的 cpu 运行队列指针 */
#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))

/* 获取当前 cpu 上的 cpu 运行队列指针 */
#define this_rq()		this_cpu_ptr(&runqueues)

/* 获取指定任务所属的 cpu 运行队列指针 */
#define task_rq(p)		cpu_rq(task_cpu(p))

/* 获取指定 cpu 上的 cpu 运行队列上当前正在运行的线程结构指针 */
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)

#define raw_rq()		raw_cpu_ptr(&runqueues)

/*********************************************************************************************************
** 函数名称: __rq_clock_broken
** 功能描述: 获取指定的 cpu 运行队列的时钟信息
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: u64 - cpu 运行队列的时钟信息，单位是 ns
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 __rq_clock_broken(struct rq *rq)
{
	return ACCESS_ONCE(rq->clock);
}

/*********************************************************************************************************
** 函数名称: rq_clock
** 功能描述: 获取指定的 cpu 运行队列的基准时钟信息，单位是 ns
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 rq_clock(struct rq *rq)
{
	lockdep_assert_held(&rq->lock);
	return rq->clock;
}

/*********************************************************************************************************
** 函数名称: rq_clock_task
** 功能描述: 获取指定的 cpu 运行队列的任务时钟信息，单位是 ns
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 rq_clock_task(struct rq *rq)
{
	lockdep_assert_held(&rq->lock);
	return rq->clock_task;
}

/* RQCF - run queue clock flags */
/* 表示发送了一个不执行运行队列的时钟信息更新操作的请求标志，详情见 __schedule 函数 */
#define RQCF_REQ_SKIP	0x01

/* 表示在调用 update_rq_clock 函数的时候，不执行运行队列的时钟信息更新操作 */
#define RQCF_ACT_SKIP	0x02

/*********************************************************************************************************
** 函数名称: rq_clock_skip_update
** 功能描述: 设置指定的 cpu 运行队列的 RQCF_REQ_SKIP 标志，表示发送一个不执行运行队列的时钟信息
**         : 更新操作的请求
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : skip - 设置或清除标志
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void rq_clock_skip_update(struct rq *rq, bool skip)
{
	lockdep_assert_held(&rq->lock);
	if (skip)
		rq->clock_skip_update |= RQCF_REQ_SKIP;
	else
		rq->clock_skip_update &= ~RQCF_REQ_SKIP;
}

#ifdef CONFIG_NUMA
/* 定义当前系统支持的节点拓扑结构类型，详情见 init_numa_topology_type 函数 */
enum numa_topology_type {
	NUMA_DIRECT,        /* 表示所有节点是直接连接在一起活着不是 NUMA 系统，他们之间可以直接通信 */
	NUMA_GLUELESS_MESH, /* 表示当前系统有的节点间通信需要经过一个中间节点才可以完成 */
	NUMA_BACKPLANE,     /* 表示当前系统有的节点间通信需要通过专有控制器才可以完成 */
};
extern enum numa_topology_type sched_numa_topology_type;
extern int sched_max_numa_distance;
extern bool find_numa_distance(int distance);
#endif

#ifdef CONFIG_NUMA_BALANCING
/* The regions in numa_faults array from task_struct */
/* 详情见 task_numa_fault 函数 */
enum numa_faults_stats {
	NUMA_MEM = 0,
	NUMA_CPU,
	NUMA_MEMBUF,
	NUMA_CPUBUF
};
extern void sched_setnuma(struct task_struct *p, int node);
extern int migrate_task_to(struct task_struct *p, int cpu);
extern int migrate_swap(struct task_struct *, struct task_struct *);
#endif /* CONFIG_NUMA_BALANCING */

#ifdef CONFIG_SMP

extern void sched_ttwu_pending(void);

#define rcu_dereference_check_sched_domain(p) \
	rcu_dereference_check((p), \
			      lockdep_is_held(&sched_domains_mutex))

/*
 * The domain tree (rq->sd) is protected by RCU's quiescent state transition.
 * See detach_destroy_domains: synchronize_sched for details.
 *
 * The domain tree of any CPU may only be accessed from within
 * preempt-disabled sections.
 */
/*********************************************************************************************************
** 函数名称: for_each_domain
** 功能描述: 从指定的 cpu 的调度域开始到所属调度域树根节点遍历路径中每一个调度域
** 输	 入: cpu - 指定的 cpu id
** 输	 出: __sd - 遍历过程中使用的临时操作变量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_domain(cpu, __sd) \
	for (__sd = rcu_dereference_check_sched_domain(cpu_rq(cpu)->sd); \
			__sd; __sd = __sd->parent)

/*********************************************************************************************************
** 函数名称: for_each_lower_domain
** 功能描述: 从指定的调度域开始遍历所有子节点
** 输	 入: sd - 指定的调度域指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_lower_domain(sd) for (; sd; sd = sd->child)

/**
 * highest_flag_domain - Return highest sched_domain containing flag.
 * @cpu:	The cpu whose highest level of sched domain is to
 *		be returned.
 * @flag:	The flag to check for the highest sched_domain
 *		for the given cpu.
 *
 * Returns the highest sched_domain of a cpu which contains the given flag.
 */
static inline struct sched_domain *highest_flag_domain(int cpu, int flag)
{
	struct sched_domain *sd, *hsd = NULL;

	for_each_domain(cpu, sd) {
		if (!(sd->flags & flag))
			break;
		hsd = sd;
	}

	return hsd;
}

/**
 * highest_flag_domain - Return lowest sched_domain containing flag.
 * @cpu:	The cpu whose lowest level of sched domain is to
 *		be returned.
 * @flag:	The flag to check for the lowest sched_domain
 *		for the given cpu.
 *
 * Returns the lowest sched_domain of a cpu which contains the given flag.
 */
static inline struct sched_domain *lowest_flag_domain(int cpu, int flag)
{
	struct sched_domain *sd;

	for_each_domain(cpu, sd) {
		if (sd->flags & flag)
			break;
	}

	return sd;
}

DECLARE_PER_CPU(struct sched_domain *, sd_llc);
DECLARE_PER_CPU(int, sd_llc_size);
DECLARE_PER_CPU(int, sd_llc_id);
DECLARE_PER_CPU(struct sched_domain *, sd_numa);
DECLARE_PER_CPU(struct sched_domain *, sd_busy);
DECLARE_PER_CPU(struct sched_domain *, sd_asym);

struct sched_group_capacity {
	atomic_t ref;
	/*
	 * CPU capacity of this group, SCHED_LOAD_SCALE being max capacity
	 * for a single CPU.
	 */
	/* 这些负载计算能力是以 1024 归一化后的数值，有关 cpu capacity 的介绍详情见
	   Documentation/devicetree/bindings/arm/cpu-capacity.txt

	   capacity - 表示当前调度组的 cpu 在指定频率状态下，除去 rt 调度类之后给 cfs 调度类剩余的以
	              SCHED_CAPACITY_SCALE 为基准值的归一化负载计算能力，单位是 DMIPS
	   capacity_orig - 表示当前调度组的 cpu 在全速（不降频）运行状态下以 SCHED_CAPACITY_SCALE 为
	              基准值的归一化负载计算能力，单位是 DMIPS
	   详情见 update_cpu_capacity 函数 */
	unsigned int capacity, capacity_orig;

	/* 表示下一次更新当前调度组负载能力信息的 jiffies 时间点，详情见 update_group_capacity 
	   函数和 update_sd_lb_stats 函数 */
	unsigned long next_update;

	/* 表示当前调度组是否因为 cpu 亲和力导致不能进行负载均衡任务迁移操作，详情见 load_balance 函数 */
	int imbalance; /* XXX unrelated to capacity but shared group state */
	
	/*
	 * Number of busy cpus in this group.
	 */
	/* 详情见 init_sched_groups_capacity 函数 */
	atomic_t nr_busy_cpus;

    /* 表示当前调度组的向上遍历迭代使用的 cpu 位图掩码值，包含了其所属父调度域包含的
       所有 cpu 位图掩码值，详情见 build_group_mask 函数和 build_sched_groups 函数 */
	unsigned long cpumask[0]; /* iteration mask */
};

struct sched_group {
	struct sched_group *next;	/* Must be a circular list */

	/* 表示当前调度组的引用计数值 */
	atomic_t ref;

	/* 表示当前调度组的负载权重信息（包含的 cpu 个数），详情见 init_sched_groups_capacity 函数 */
	unsigned int group_weight;

	/* 表示当前调度组的负载计算能力信息 */
	struct sched_group_capacity *sgc;

	/*
	 * The CPUs this group covers.
	 *
	 * NOTE: this field is variable length. (Allocated dynamically
	 * by attaching extra space to the end of the structure,
	 * depending on how many CPUs the kernel has booted up with)
	 */
	/* 表示当前调度组内包含的 cpu 位图，详情见 build_overlap_sched_groups 函数和 build_sched_groups 函数 */
	unsigned long cpumask[0];
};

/*********************************************************************************************************
** 函数名称: sched_group_cpus
** 功能描述: 获取指定的调度组内包含的 cpu 位图变量指针
** 输	 入: sg - 指定的调度组指针
** 输	 出: struct cpumask * - cpu 位图变量指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cpumask *sched_group_cpus(struct sched_group *sg)
{
	return to_cpumask(sg->cpumask);
}

/*
 * cpumask masking which cpus in the group are allowed to iterate up the domain
 * tree.
 */
/*********************************************************************************************************
** 函数名称: sched_group_mask
** 功能描述: 获取指定的调度组向上遍历迭代使用的 cpu 位图掩码变量指针
** 输	 入: group - 指定的调度组指针
** 输	 出: cpumask - cpu 的位图掩码值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cpumask *sched_group_mask(struct sched_group *sg)
{
	return to_cpumask(sg->sgc->cpumask);
}

/**
 * group_first_cpu - Returns the first cpu in the cpumask of a sched_group.
 * @group: The group whose first cpu is to be returned.
 */
/*********************************************************************************************************
** 函数名称: group_first_cpu
** 功能描述: 获取指定的调度组内第一个 cpu 的 id 值，也是 cpu id 值最小的 cpu
** 输	 入: group - 指定的调度组指针
** 输	 出: unsigned int - 第一个 cpu 的 id 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned int group_first_cpu(struct sched_group *group)
{
	return cpumask_first(sched_group_cpus(group));
}

extern int group_balance_cpu(struct sched_group *sg);

#else

/*********************************************************************************************************
** 函数名称: sched_ttwu_pending
** 功能描述: 唤醒当前正在运行的 cpu 运行队列上所有被挂起且待唤醒的每一个任务
** 输	 入: p - 指定的被唤醒任务指针
**         : wake_flags - 指定的 wakeup flags，例如 WF_FORK
** 输	 出: 1 - 执行成功
**         : 0 - 执行失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void sched_ttwu_pending(void) { }

#endif /* CONFIG_SMP */

#include "stats.h"
#include "auto_group.h"

#ifdef CONFIG_CGROUP_SCHED

/*
 * Return the group to which this tasks belongs.
 *
 * We cannot use task_css() and friends because the cgroup subsystem
 * changes that value before the cgroup_subsys::attach() method is called,
 * therefore we cannot pin it and might observe the wrong value.
 *
 * The same is true for autogroup's p->signal->autogroup->tg, the autogroup
 * core changes this before calling sched_move_task().
 *
 * Instead we use a 'copy' which is updated from sched_move_task() while
 * holding both task_struct::pi_lock and rq::lock.
 */
/*********************************************************************************************************
** 函数名称: task_group
** 功能描述: 获取指定任务的调度任务组信息
** 输	 入: p - 指定的 task_struct 结构指针
** 输	 出: task_group * - 获取到的任务组信息指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct task_group *task_group(struct task_struct *p)
{
	return p->sched_task_group;
}

/* Change a task's cfs_rq and parent entity if it moves across CPUs/groups */
/*********************************************************************************************************
** 函数名称: set_task_rq
** 功能描述: 设置指定的任务的运行队列信息为指定 cpu 的运行队列信息
** 输	 入: p - 指定的 task_struct 结构指针
**         : cpu - 指定的 cpu 号
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void set_task_rq(struct task_struct *p, unsigned int cpu)
{
#if defined(CONFIG_FAIR_GROUP_SCHED) || defined(CONFIG_RT_GROUP_SCHED)
	struct task_group *tg = task_group(p);
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
	p->se.cfs_rq = tg->cfs_rq[cpu];
	p->se.parent = tg->se[cpu];
#endif

#ifdef CONFIG_RT_GROUP_SCHED
	p->rt.rt_rq  = tg->rt_rq[cpu];
	p->rt.parent = tg->rt_se[cpu];
#endif
}

#else /* CONFIG_CGROUP_SCHED */

static inline void set_task_rq(struct task_struct *p, unsigned int cpu) { }
static inline struct task_group *task_group(struct task_struct *p)
{
	return NULL;
}

#endif /* CONFIG_CGROUP_SCHED */

/*********************************************************************************************************
** 函数名称: __set_task_cpu
** 功能描述: 把指定的任务分配到指定的 cpu 的运行队列上并更新相关信息
** 输	 入: p - 指定的 task_struct 结构指针
**         : cpu - 指定的 cpu 号
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __set_task_cpu(struct task_struct *p, unsigned int cpu)
{
	set_task_rq(p, cpu);
#ifdef CONFIG_SMP
	/*
	 * After ->cpu is set up to a new value, task_rq_lock(p, ...) can be
	 * successfuly executed on another CPU. We must ensure that updates of
	 * per-task data have been completed by this moment.
	 */
	smp_wmb();
	task_thread_info(p)->cpu = cpu;
	p->wake_cpu = cpu;
#endif
}

/*
 * Tunables that become constants when CONFIG_SCHED_DEBUG is off:
 */
#ifdef CONFIG_SCHED_DEBUG
# include <linux/static_key.h>
# define const_debug __read_mostly
#else
# define const_debug const
#endif

extern const_debug unsigned int sysctl_sched_features;

#define SCHED_FEAT(name, enabled)	\
	__SCHED_FEAT_##name ,

enum {
#include "features.h"
	__SCHED_FEAT_NR,
};

#undef SCHED_FEAT

#if defined(CONFIG_SCHED_DEBUG) && defined(HAVE_JUMP_LABEL)
#define SCHED_FEAT(name, enabled)					\
static __always_inline bool static_branch_##name(struct static_key *key) \
{									\
	return static_key_##enabled(key);				\
}

#include "features.h"

#undef SCHED_FEAT

extern struct static_key sched_feat_keys[__SCHED_FEAT_NR];
#define sched_feat(x) (static_branch_##x(&sched_feat_keys[__SCHED_FEAT_##x]))
#else /* !(SCHED_DEBUG && HAVE_JUMP_LABEL) */
#define sched_feat(x) (sysctl_sched_features & (1UL << __SCHED_FEAT_##x))
#endif /* SCHED_DEBUG && HAVE_JUMP_LABEL */

#ifdef CONFIG_NUMA_BALANCING
#define sched_feat_numa(x) sched_feat(x)
#ifdef CONFIG_SCHED_DEBUG
#define numabalancing_enabled sched_feat_numa(NUMA)
#else
extern bool numabalancing_enabled;
#endif /* CONFIG_SCHED_DEBUG */
#else
#define sched_feat_numa(x) (0)
#define numabalancing_enabled (0)
#endif /* CONFIG_NUMA_BALANCING */

static inline u64 global_rt_period(void)
{
	return (u64)sysctl_sched_rt_period * NSEC_PER_USEC;
}

static inline u64 global_rt_runtime(void)
{
	if (sysctl_sched_rt_runtime < 0)
		return RUNTIME_INF;

	return (u64)sysctl_sched_rt_runtime * NSEC_PER_USEC;
}

/*********************************************************************************************************
** 函数名称: task_current
** 功能描述: 判断指定的任务是否是指定的 cpu 运行队列当前正在运行的任务
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_current(struct rq *rq, struct task_struct *p)
{
	return rq->curr == p;
}

/*********************************************************************************************************
** 函数名称: task_running
** 功能描述: 判断指定的任务是否正在 cpu 上运行
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : p - 指定的任务指针
** 输	 出: i - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_running(struct rq *rq, struct task_struct *p)
{
#ifdef CONFIG_SMP
	return p->on_cpu;
#else
	return task_current(rq, p);
#endif
}

/*********************************************************************************************************
** 函数名称: task_on_rq_queued
** 功能描述: 判断指定的任务是否在所属 cpu 运行队列上
** 输	 入: p - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_on_rq_queued(struct task_struct *p)
{
	return p->on_rq == TASK_ON_RQ_QUEUED;
}

/*********************************************************************************************************
** 函数名称: task_on_rq_migrating
** 功能描述: 判断指定的任务是否处于 TASK_ON_RQ_MIGRATING 状态
** 输	 入: p - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_on_rq_migrating(struct task_struct *p)
{
	return p->on_rq == TASK_ON_RQ_MIGRATING;
}

#ifndef prepare_arch_switch
# define prepare_arch_switch(next)	do { } while (0)
#endif
#ifndef finish_arch_switch
# define finish_arch_switch(prev)	do { } while (0)
#endif
#ifndef finish_arch_post_lock_switch
# define finish_arch_post_lock_switch()	do { } while (0)
#endif

/*********************************************************************************************************
** 函数名称: prepare_lock_switch
** 功能描述: 在指定的 cpu 运行队列上执行完任务切换前调用，用来切换 cpu 运行队列锁持有者
** 注     释: 这个函数调用之前需要和 finish_lock_switch 函数成对的调用
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : next - 指定的将要被切换进来的将要运行的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void prepare_lock_switch(struct rq *rq, struct task_struct *next)
{
#ifdef CONFIG_SMP
	/*
	 * We can optimise this out completely for !SMP, because the
	 * SMP rebalancing from interrupt is the only thing that cares
	 * here.
	 */
	next->on_cpu = 1;
#endif
}

/*********************************************************************************************************
** 函数名称: finish_lock_switch
** 功能描述: 在指定的 cpu 运行队列上执行完任务切换后调用，用来切换 cpu 运行队列锁持有者
** 注     释: 这个函数调用之前需要和 finish_lock_switch 函数成对的调用
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : prev - 指定的将要被切换出的当前正在运行的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
{
#ifdef CONFIG_SMP
	/*
	 * After ->on_cpu is cleared, the task can be moved to a different CPU.
	 * We must ensure this doesn't happen until the switch is completely
	 * finished.
	 */
	smp_wmb();
	prev->on_cpu = 0;
#endif
#ifdef CONFIG_DEBUG_SPINLOCK
	/* this is a valid case when another task releases the spinlock */
	rq->lock.owner = current;
#endif
	/*
	 * If we are tracking spinlock dependencies then we have to
	 * fix up the runqueue lock - which gets 'carried over' from
	 * prev into current:
	 */
	spin_acquire(&rq->lock.dep_map, 0, 0, _THIS_IP_);

	raw_spin_unlock_irq(&rq->lock);
}

/*
 * wake flags
 */
#define WF_SYNC		0x01		/* waker goes to sleep after wakeup */
#define WF_FORK		0x02		/* child wakeup after fork */
#define WF_MIGRATED	0x4		    /* internal use, task got migrated */

/*
 * To aid in avoiding the subversion of "niceness" due to uneven distribution
 * of tasks with abnormal "nice" values across CPUs the contribution that
 * each task makes to its run queue's load is weighted according to its
 * scheduling class and "nice" value. For SCHED_NORMAL tasks this is just a
 * scaled version of the new time slice allocation that they receive on time
 * slice expiry etc.
 */

#define WEIGHT_IDLEPRIO                3
#define WMULT_IDLEPRIO         1431655765

/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage. (to achieve that we use a multiplier of 1.25.
 * If a task goes up by ~10% and another task goes down by ~10% then
 * the relative distance between them is ~25%.)
 */
static const int prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};

/*
 * Inverse (2^32/x) values of the prio_to_weight[] array, precalculated.
 *
 * In cases where the weight does not change often, we can use the
 * precalculated inverse to speed up arithmetics by turning divisions
 * into multiplications:
 */
static const u32 prio_to_wmult[40] = {
 /* -20 */     48388,     59856,     76040,     92818,    118348,
 /* -15 */    147320,    184698,    229616,    287308,    360437,
 /* -10 */    449829,    563644,    704093,    875809,   1099582,
 /*  -5 */   1376151,   1717300,   2157191,   2708050,   3363326,
 /*   0 */   4194304,   5237765,   6557202,   8165337,  10153587,
 /*   5 */  12820798,  15790321,  19976592,  24970740,  31350126,
 /*  10 */  39045157,  49367440,  61356676,  76695844,  95443717,
 /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};

/* 表示在把指定的调度实例添加到运行队列中后要及时唤醒并运行这个调度实例 */
#define ENQUEUE_WAKEUP		1

#define ENQUEUE_HEAD		2
#ifdef CONFIG_SMP
#define ENQUEUE_WAKING		4	/* sched_class::task_waking was called */
#else
#define ENQUEUE_WAKING		0
#endif
#define ENQUEUE_REPLENISH	8

/* 表示在把指定的调度实例从运行队列中移除后会进入睡眠状态 */
#define DEQUEUE_SLEEP		1

/* 表示从当前调度类中没有找到合适的、待执行的任务，需要从其他调度类中查找，详情见 pick_next_task 函数 */
#define RETRY_TASK		((void *)-1UL)

struct sched_class {
	const struct sched_class *next;

	void (*enqueue_task) (struct rq *rq, struct task_struct *p, int flags);
	void (*dequeue_task) (struct rq *rq, struct task_struct *p, int flags);
	void (*yield_task) (struct rq *rq);
	bool (*yield_to_task) (struct rq *rq, struct task_struct *p, bool preempt);

	void (*check_preempt_curr) (struct rq *rq, struct task_struct *p, int flags);

	/*
	 * It is the responsibility of the pick_next_task() method that will
	 * return the next task to call put_prev_task() on the @prev task or
	 * something equivalent.
	 *
	 * May return RETRY_TASK when it finds a higher prio class has runnable
	 * tasks.
	 */
	struct task_struct * (*pick_next_task) (struct rq *rq,
						struct task_struct *prev);
	void (*put_prev_task) (struct rq *rq, struct task_struct *p);

#ifdef CONFIG_SMP
	int  (*select_task_rq)(struct task_struct *p, int task_cpu, int sd_flag, int flags);
	void (*migrate_task_rq)(struct task_struct *p, int next_cpu);

	void (*post_schedule) (struct rq *this_rq);
	void (*task_waking) (struct task_struct *task);
	void (*task_woken) (struct rq *this_rq, struct task_struct *task);

	void (*set_cpus_allowed)(struct task_struct *p,
				 const struct cpumask *newmask);

	void (*rq_online)(struct rq *rq);
	void (*rq_offline)(struct rq *rq);
#endif

	void (*set_curr_task) (struct rq *rq);
	void (*task_tick) (struct rq *rq, struct task_struct *p, int queued);
	void (*task_fork) (struct task_struct *p);
	void (*task_dead) (struct task_struct *p);

	/*
	 * The switched_from() call is allowed to drop rq->lock, therefore we
	 * cannot assume the switched_from/switched_to pair is serliazed by
	 * rq->lock. They are however serialized by p->pi_lock.
	 */
	void (*switched_from) (struct rq *this_rq, struct task_struct *task);
	void (*switched_to) (struct rq *this_rq, struct task_struct *task);
	void (*prio_changed) (struct rq *this_rq, struct task_struct *task,
			     int oldprio);

	unsigned int (*get_rr_interval) (struct rq *rq,
					 struct task_struct *task);

	void (*update_curr) (struct rq *rq);

#ifdef CONFIG_FAIR_GROUP_SCHED
	void (*task_move_group) (struct task_struct *p, int on_rq);
#endif
};

/*********************************************************************************************************
** 函数名称: put_prev_entity
** 功能描述: 把指定的当前正在运行的调度实例放回到所属 cpu 运行队列的上并更新相关调度统计值
** 输	 入: rq- 指定的 cpu 运行队列指针
**         : prev - 指定的当前正在运行的调度实例指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void put_prev_task(struct rq *rq, struct task_struct *prev)
{
	prev->sched_class->put_prev_task(rq, prev);
}

/* 表示当前系统内优先级最高的调度类指针 */
#define sched_class_highest (&stop_sched_class)

/*********************************************************************************************************
** 函数名称: for_each_class
** 功能描述: 遍历当前系统内支持的每一个调度类，按照优先级从高到底的顺序遍历
** 输	 入: class - 遍历过程中使用的可操作临时变量
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define for_each_class(class) \
   for (class = sched_class_highest; class; class = class->next)

extern const struct sched_class stop_sched_class;
extern const struct sched_class dl_sched_class;
extern const struct sched_class rt_sched_class;
extern const struct sched_class fair_sched_class;
extern const struct sched_class idle_sched_class;


#ifdef CONFIG_SMP

extern void update_group_capacity(struct sched_domain *sd, int cpu);

extern void trigger_load_balance(struct rq *rq);

extern void idle_enter_fair(struct rq *this_rq);
extern void idle_exit_fair(struct rq *this_rq);

#else

static inline void idle_enter_fair(struct rq *rq) { }
static inline void idle_exit_fair(struct rq *rq) { }

#endif

#ifdef CONFIG_CPU_IDLE
static inline void idle_set_state(struct rq *rq,
				  struct cpuidle_state *idle_state)
{
	rq->idle_state = idle_state;
}

static inline struct cpuidle_state *idle_get_state(struct rq *rq)
{
	WARN_ON(!rcu_read_lock_held());
	return rq->idle_state;
}
#else
static inline void idle_set_state(struct rq *rq,
				  struct cpuidle_state *idle_state)
{
}

static inline struct cpuidle_state *idle_get_state(struct rq *rq)
{
	return NULL;
}
#endif

extern void sysrq_sched_debug_show(void);
extern void sched_init_granularity(void);
extern void update_max_interval(void);

extern void init_sched_dl_class(void);
extern void init_sched_rt_class(void);
extern void init_sched_fair_class(void);
extern void init_sched_dl_class(void);

extern void resched_curr(struct rq *rq);
extern void resched_cpu(int cpu);

extern struct rt_bandwidth def_rt_bandwidth;
extern void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime);

extern struct dl_bandwidth def_dl_bandwidth;
extern void init_dl_bandwidth(struct dl_bandwidth *dl_b, u64 period, u64 runtime);
extern void init_dl_task_timer(struct sched_dl_entity *dl_se);

unsigned long to_ratio(u64 period, u64 runtime);

extern void update_idle_cpu_load(struct rq *this_rq);

extern void init_task_runnable_average(struct task_struct *p);

/*********************************************************************************************************
** 函数名称: add_nr_running
** 功能描述: 把指定的 cpu 运行队列的 rq->nr_running 增加指定的增量值
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : count - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void add_nr_running(struct rq *rq, unsigned count)
{
	unsigned prev_nr = rq->nr_running;

	rq->nr_running = prev_nr + count;

	if (prev_nr < 2 && rq->nr_running >= 2) {
#ifdef CONFIG_SMP
		if (!rq->rd->overload)
			rq->rd->overload = true;
#endif

#ifdef CONFIG_NO_HZ_FULL
		if (tick_nohz_full_cpu(rq->cpu)) {
			/*
			 * Tick is needed if more than one task runs on a CPU.
			 * Send the target an IPI to kick it out of nohz mode.
			 *
			 * We assume that IPI implies full memory barrier and the
			 * new value of rq->nr_running is visible on reception
			 * from the target.
			 */
			tick_nohz_full_kick_cpu(rq->cpu);
		}
#endif
	}
}

/*********************************************************************************************************
** 函数名称: sub_nr_running
** 功能描述: 把指定的 cpu 运行队列的调度实例统计变量减去指定的值
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : count - 指定的减量
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void sub_nr_running(struct rq *rq, unsigned count)
{
	rq->nr_running -= count;
}

/*********************************************************************************************************
** 函数名称: rq_last_tick_reset
** 功能描述: 更新指定的 cpu 运行队列的 last_sched_tick 字段值
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void rq_last_tick_reset(struct rq *rq)
{
#ifdef CONFIG_NO_HZ_FULL
	rq->last_sched_tick = jiffies;
#endif
}

extern void update_rq_clock(struct rq *rq);

extern void activate_task(struct rq *rq, struct task_struct *p, int flags);
extern void deactivate_task(struct rq *rq, struct task_struct *p, int flags);

extern void check_preempt_curr(struct rq *rq, struct task_struct *p, int flags);

extern const_debug unsigned int sysctl_sched_time_avg;
extern const_debug unsigned int sysctl_sched_nr_migrate;
extern const_debug unsigned int sysctl_sched_migration_cost;

/*********************************************************************************************************
** 函数名称: sched_avg_period
** 功能描述: 获取当前调度系统中 cfs 调度类的基准分时周期，单位是 ns
** 注     释: 当前系统为了在实时调度类中任务过载情况下不会把 cfs 调度类任务饿死，为 cfs 调度类分配了
**         : 基准分时周期，即在一个调度分时周期内，至少有一半时间会分配给 cfs 调度类
** 输	 入: 
** 输	 出: u64 - cfs 调度类的基准分时周期，单位是 ns(0.5S)
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 sched_avg_period(void)
{
	return (u64)sysctl_sched_time_avg * NSEC_PER_MSEC / 2;
}

#ifdef CONFIG_SCHED_HRTICK

/*
 * Use hrtick when:
 *  - enabled by features
 *  - hrtimer is actually high res
 */
/*********************************************************************************************************
** 函数名称: hrtick_enabled
** 功能描述: 判断指定的 cpu 运行队列的任务调度高精度定时器是否已经使能
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 1 - 已经使能
**         : 0 - 没使能
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int hrtick_enabled(struct rq *rq)
{
	if (!sched_feat(HRTICK))
		return 0;
	if (!cpu_active(cpu_of(rq)))
		return 0;
	return hrtimer_is_hres_active(&rq->hrtick_timer);
}

void hrtick_start(struct rq *rq, u64 delay);

#else

/*********************************************************************************************************
** 函数名称: hrtick_enabled
** 功能描述: 判断指定的 cpu 运行队列的任务调度高精度定时器是否已经使能
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 1 - 已经使能
**         : 0 - 没使能
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int hrtick_enabled(struct rq *rq)
{
	return 0;
}

#endif /* CONFIG_SCHED_HRTICK */

#ifdef CONFIG_SMP

/*********************************************************************************************************
** 函数名称: sched_avg_update
** 功能描述: 更新指定的 cpu 运行队列的运行时间统计值
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
extern void sched_avg_update(struct rq *rq);

/*********************************************************************************************************
** 函数名称: sched_rt_avg_update
** 功能描述: 更新指定的 cpu 运行队列的实时调度实例的运行时间统计值
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : rt_delta - 实时调度实例的运行时间增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void sched_rt_avg_update(struct rq *rq, u64 rt_delta)
{
	rq->rt_avg += rt_delta;
	sched_avg_update(rq);
}
#else

/*********************************************************************************************************
** 函数名称: sched_rt_avg_update
** 功能描述: 更新指定的 cpu 运行队列的实时调度实例的运行时间统计值
** 输	 入: rq - 指定的 cpu 运行队列指针
**         : rt_delta - 实时调度实例的运行时间增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void sched_rt_avg_update(struct rq *rq, u64 rt_delta) { }

/*********************************************************************************************************
** 函数名称: sched_avg_update
** 功能描述: 更新指定的 cpu 运行队列的运行时间统计值
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void sched_avg_update(struct rq *rq) { }
#endif

/*********************************************************************************************************
** 函数名称: start_bandwidth_timer
** 功能描述: 使指定的带宽控制高精度定时器按照指定的超时周期启动
** 输	 入: period_timer - 指定的带宽控制高精度定时器指针
**         : period - 指定的超时周期
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
extern void start_bandwidth_timer(struct hrtimer *period_timer, ktime_t period);

/*
 * __task_rq_lock - lock the rq @p resides on.
 */
/*********************************************************************************************************
** 函数名称: __task_rq_lock
** 功能描述: 获取指定任务所在的运行队列的锁并返回这个运行队列的指针
** 输	 入: p - 定的的 task_struct 结构指针
** 输	 出: q * - 成功获取锁的运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct rq *__task_rq_lock(struct task_struct *p)
	__acquires(rq->lock)
{
	struct rq *rq;

	lockdep_assert_held(&p->pi_lock);

	for (;;) {
		rq = task_rq(p);
		raw_spin_lock(&rq->lock);
		if (likely(rq == task_rq(p) && !task_on_rq_migrating(p)))
			return rq;
		raw_spin_unlock(&rq->lock);

		while (unlikely(task_on_rq_migrating(p)))
			cpu_relax();
	}
}

/*
 * task_rq_lock - lock p->pi_lock and lock the rq @p resides on.
 */
/*********************************************************************************************************
** 函数名称: task_rq_lock
** 功能描述: 获取指定任务以及所属 cpu 运行队列的锁并返回运行队列指针
** 输	 入: p - 指定的任务指针
** 输	 出: flags - 存储中断标志信息
**         : rq - 获取到的 cpu 运行队列指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct rq *task_rq_lock(struct task_struct *p, unsigned long *flags)
	__acquires(p->pi_lock)
	__acquires(rq->lock)
{
	struct rq *rq;

	for (;;) {
		raw_spin_lock_irqsave(&p->pi_lock, *flags);
		rq = task_rq(p);
		raw_spin_lock(&rq->lock);
		/*
		 *	move_queued_task()		task_rq_lock()
		 *
		 *	ACQUIRE (rq->lock)
		 *	[S] ->on_rq = MIGRATING		[L] rq = task_rq()
		 *	WMB (__set_task_cpu())		ACQUIRE (rq->lock);
		 *	[S] ->cpu = new_cpu		[L] task_rq()
		 *					[L] ->on_rq
		 *	RELEASE (rq->lock)
		 *
		 * If we observe the old cpu in task_rq_lock, the acquire of
		 * the old rq->lock will fully serialize against the stores.
		 *
		 * If we observe the new cpu in task_rq_lock, the acquire will
		 * pair with the WMB to ensure we must then also see migrating.
		 */
		if (likely(rq == task_rq(p) && !task_on_rq_migrating(p)))
			return rq;
		raw_spin_unlock(&rq->lock);
		raw_spin_unlock_irqrestore(&p->pi_lock, *flags);

		while (unlikely(task_on_rq_migrating(p)))
			cpu_relax();
	}
}

/*********************************************************************************************************
** 函数名称: __task_rq_unlock
** 功能描述: 释放指定的 cpu 运行队列指锁
** 输	 入: rq - 指定的 cpu 运行队列指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void __task_rq_unlock(struct rq *rq)
	__releases(rq->lock)
{
	raw_spin_unlock(&rq->lock);
}

/*********************************************************************************************************
** 函数名称: task_rq_unlock
** 功能描述: 释放指定任务以及所属 cpu 运行队列指锁
** 输	 入: rq - 所属 cpu 运行队列指针
**         : p - 指定的任务指针
**         : flags - 存储中断标志信息
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void
task_rq_unlock(struct rq *rq, struct task_struct *p, unsigned long *flags)
	__releases(rq->lock)
	__releases(p->pi_lock)
{
	raw_spin_unlock(&rq->lock);
	raw_spin_unlock_irqrestore(&p->pi_lock, *flags);
}

#ifdef CONFIG_SMP
#ifdef CONFIG_PREEMPT

static inline void double_rq_lock(struct rq *rq1, struct rq *rq2);

/*
 * fair double_lock_balance: Safely acquires both rq->locks in a fair
 * way at the expense of forcing extra atomic operations in all
 * invocations.  This assures that the double_lock is acquired using the
 * same underlying policy as the spinlock_t on this architecture, which
 * reduces latency compared to the unfair variant below.  However, it
 * also adds more overhead and therefore may reduce throughput.
 */
static inline int _double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	raw_spin_unlock(&this_rq->lock);
	double_rq_lock(this_rq, busiest);

	return 1;
}

#else
/*
 * Unfair double_lock_balance: Optimizes throughput at the expense of
 * latency by eliminating extra atomic operations when the locks are
 * already in proper order on entry.  This favors lower cpu-ids and will
 * grant the double lock to lower cpus over higher ids under contention,
 * regardless of entry order into the function.
 */
static inline int _double_lock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(this_rq->lock)
	__acquires(busiest->lock)
	__acquires(this_rq->lock)
{
	int ret = 0;

	if (unlikely(!raw_spin_trylock(&busiest->lock))) {
		if (busiest < this_rq) {
			raw_spin_unlock(&this_rq->lock);
			raw_spin_lock(&busiest->lock);
			raw_spin_lock_nested(&this_rq->lock,
					      SINGLE_DEPTH_NESTING);
			ret = 1;
		} else
			raw_spin_lock_nested(&busiest->lock,
					      SINGLE_DEPTH_NESTING);
	}
	return ret;
}

#endif /* CONFIG_PREEMPT */

/*
 * double_lock_balance - lock the busiest runqueue, this_rq is locked already.
 */
static inline int double_lock_balance(struct rq *this_rq, struct rq *busiest)
{
	if (unlikely(!irqs_disabled())) {
		/* printk() doesn't work good under rq->lock */
		raw_spin_unlock(&this_rq->lock);
		BUG_ON(1);
	}

	return _double_lock_balance(this_rq, busiest);
}

static inline void double_unlock_balance(struct rq *this_rq, struct rq *busiest)
	__releases(busiest->lock)
{
	raw_spin_unlock(&busiest->lock);
	lock_set_subclass(&this_rq->lock.dep_map, 0, _RET_IP_);
}

static inline void double_lock(spinlock_t *l1, spinlock_t *l2)
{
	if (l1 > l2)
		swap(l1, l2);

	spin_lock(l1);
	spin_lock_nested(l2, SINGLE_DEPTH_NESTING);
}

static inline void double_lock_irq(spinlock_t *l1, spinlock_t *l2)
{
	if (l1 > l2)
		swap(l1, l2);

	spin_lock_irq(l1);
	spin_lock_nested(l2, SINGLE_DEPTH_NESTING);
}

static inline void double_raw_lock(raw_spinlock_t *l1, raw_spinlock_t *l2)
{
	if (l1 > l2)
		swap(l1, l2);

	raw_spin_lock(l1);
	raw_spin_lock_nested(l2, SINGLE_DEPTH_NESTING);
}

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static inline void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	if (rq1 == rq2) {
		raw_spin_lock(&rq1->lock);
		__acquire(rq2->lock);	/* Fake it out ;) */
	} else {
		if (rq1 < rq2) {
			raw_spin_lock(&rq1->lock);
			raw_spin_lock_nested(&rq2->lock, SINGLE_DEPTH_NESTING);
		} else {
			raw_spin_lock(&rq2->lock);
			raw_spin_lock_nested(&rq1->lock, SINGLE_DEPTH_NESTING);
		}
	}
}

/*
 * double_rq_unlock - safely unlock two runqueues
 *
 * Note this does not restore interrupts like task_rq_unlock,
 * you need to do so manually after calling.
 */
static inline void double_rq_unlock(struct rq *rq1, struct rq *rq2)
	__releases(rq1->lock)
	__releases(rq2->lock)
{
	raw_spin_unlock(&rq1->lock);
	if (rq1 != rq2)
		raw_spin_unlock(&rq2->lock);
	else
		__release(rq2->lock);
}

#else /* CONFIG_SMP */

/*
 * double_rq_lock - safely lock two runqueues
 *
 * Note this does not disable interrupts like task_rq_lock,
 * you need to do so manually before calling.
 */
static inline void double_rq_lock(struct rq *rq1, struct rq *rq2)
	__acquires(rq1->lock)
	__acquires(rq2->lock)
{
	BUG_ON(!irqs_disabled());
	BUG_ON(rq1 != rq2);
	raw_spin_lock(&rq1->lock);
	__acquire(rq2->lock);	/* Fake it out ;) */
}

/*
 * double_rq_unlock - safely unlock two runqueues
 *
 * Note this does not restore interrupts like task_rq_unlock,
 * you need to do so manually after calling.
 */
static inline void double_rq_unlock(struct rq *rq1, struct rq *rq2)
	__releases(rq1->lock)
	__releases(rq2->lock)
{
	BUG_ON(rq1 != rq2);
	raw_spin_unlock(&rq1->lock);
	__release(rq2->lock);
}

#endif

extern struct sched_entity *__pick_first_entity(struct cfs_rq *cfs_rq);
extern struct sched_entity *__pick_last_entity(struct cfs_rq *cfs_rq);
extern void print_cfs_stats(struct seq_file *m, int cpu);
extern void print_rt_stats(struct seq_file *m, int cpu);
extern void print_dl_stats(struct seq_file *m, int cpu);

extern void init_cfs_rq(struct cfs_rq *cfs_rq);
extern void init_rt_rq(struct rt_rq *rt_rq, struct rq *rq);
extern void init_dl_rq(struct dl_rq *dl_rq, struct rq *rq);

extern void cfs_bandwidth_usage_inc(void);
extern void cfs_bandwidth_usage_dec(void);

#ifdef CONFIG_NO_HZ_COMMON
enum rq_nohz_flag_bits {
    /* 表示当前 cpu 在 tick 中断退出时处于 idle 状态，这样我们就可以在这个 cpu 上执行 idle 负载均衡操作了 */
	NOHZ_TICK_STOPPED,

    /* 表示当前 cpu 需要执行一次 nohz 负载均衡操作，详情见 nohz_balancer_kick 函数和 nohz_idle_balance 函数 */
	NOHZ_BALANCE_KICK,
};

/*********************************************************************************************************
** 函数名称: nohz_flags
** 功能描述: 获取指定 cpu 的 cpu 运行队列的 nohz 标志值
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: nohz_flags - nohz 标志值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define nohz_flags(cpu)	(&cpu_rq(cpu)->nohz_flags)
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING

DECLARE_PER_CPU(u64, cpu_hardirq_time);
DECLARE_PER_CPU(u64, cpu_softirq_time);

#ifndef CONFIG_64BIT
DECLARE_PER_CPU(seqcount_t, irq_time_seq);

static inline void irq_time_write_begin(void)
{
	__this_cpu_inc(irq_time_seq.sequence);
	smp_wmb();
}

static inline void irq_time_write_end(void)
{
	smp_wmb();
	__this_cpu_inc(irq_time_seq.sequence);
}

/*********************************************************************************************************
** 函数名称: irq_time_read
** 功能描述: 获取指定的 cpu 在中断上下文执行时间统计变量值
** 输	 入: cpu - 定的 cpu id
** 输	 出: irq_time - 中断上下文执行的时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 irq_time_read(int cpu)
{
	u64 irq_time;
	unsigned seq;

	do {
		seq = read_seqcount_begin(&per_cpu(irq_time_seq, cpu));
		irq_time = per_cpu(cpu_softirq_time, cpu) +
			   per_cpu(cpu_hardirq_time, cpu);
	} while (read_seqcount_retry(&per_cpu(irq_time_seq, cpu), seq));

	return irq_time;
}
#else /* CONFIG_64BIT */
static inline void irq_time_write_begin(void)
{
}

static inline void irq_time_write_end(void)
{
}

/*********************************************************************************************************
** 函数名称: irq_time_read
** 功能描述: 获取指定的 cpu 在中断上下文执行时间统计变量值
** 输	 入: cpu - 定的 cpu id
** 输	 出: irq_time - 中断上下文执行的时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 irq_time_read(int cpu)
{
	return per_cpu(cpu_softirq_time, cpu) + per_cpu(cpu_hardirq_time, cpu);
}
#endif /* CONFIG_64BIT */
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */
