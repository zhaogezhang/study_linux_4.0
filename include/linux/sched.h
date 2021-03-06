#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

#include <uapi/linux/sched.h>

#include <linux/sched/prio.h>


struct sched_param {
	int sched_priority;
};

#include <asm/param.h>	/* for HZ */

#include <linux/capability.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timex.h>
#include <linux/jiffies.h>
#include <linux/plist.h>
#include <linux/rbtree.h>
#include <linux/thread_info.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/nodemask.h>
#include <linux/mm_types.h>
#include <linux/preempt_mask.h>

#include <asm/page.h>
#include <asm/ptrace.h>
#include <linux/cputime.h>

#include <linux/smp.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/signal.h>
#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/pid.h>
#include <linux/percpu.h>
#include <linux/topology.h>
#include <linux/proportions.h>
#include <linux/seccomp.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/rtmutex.h>

#include <linux/time.h>
#include <linux/param.h>
#include <linux/resource.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/task_io_accounting.h>
#include <linux/latencytop.h>
#include <linux/cred.h>
#include <linux/llist.h>
#include <linux/uidgid.h>
#include <linux/gfp.h>
#include <linux/magic.h>

#include <asm/processor.h>

#define SCHED_ATTR_SIZE_VER0	48	/* sizeof first published struct */

/*
 * Extended scheduling parameters data structure.
 *
 * This is needed because the original struct sched_param can not be
 * altered without introducing ABI issues with legacy applications
 * (e.g., in sched_getparam()).
 *
 * However, the possibility of specifying more than just a priority for
 * the tasks may be useful for a wide variety of application fields, e.g.,
 * multimedia, streaming, automation and control, and many others.
 *
 * This variant (sched_attr) is meant at describing a so-called
 * sporadic time-constrained task. In such model a task is specified by:
 *  - the activation period or minimum instance inter-arrival time;
 *  - the maximum (or average, depending on the actual scheduling
 *    discipline) computation time of all instances, a.k.a. runtime;
 *  - the deadline (relative to the actual activation time) of each
 *    instance.
 * Very briefly, a periodic (sporadic) task asks for the execution of
 * some specific computation --which is typically called an instance--
 * (at most) every period. Moreover, each instance typically lasts no more
 * than the runtime and must be completed by time instant t equal to
 * the instance activation time + the deadline.
 *
 * This is reflected by the actual fields of the sched_attr structure:
 *
 *  @size		size of the structure, for fwd/bwd compat.
 *
 *  @sched_policy	task's scheduling policy
 *  @sched_flags	for customizing the scheduler behaviour
 *  @sched_nice		task's nice value      (SCHED_NORMAL/BATCH)
 *  @sched_priority	task's static priority (SCHED_FIFO/RR)
 *  @sched_deadline	representative of the task's deadline
 *  @sched_runtime	representative of the task's runtime
 *  @sched_period	representative of the task's period
 *
 * Given this task model, there are a multiplicity of scheduling algorithms
 * and policies, that can be used to ensure all the tasks will make their
 * timing constraints.
 *
 * As of now, the SCHED_DEADLINE policy (sched_dl scheduling class) is the
 * only user of this new interface. More information about the algorithm
 * available in the scheduling class file or in Documentation/.
 */
struct sched_attr {
    /* 表示当前结构体有效数据字节数 */
	u32 size;

    /* 指定的新的调度策略 */
	u32 sched_policy;

	/* 指定的调度标志，例如 SCHED_FLAG_RESET_ON_FORK */
	u64 sched_flags;

	/* SCHED_NORMAL, SCHED_BATCH */
	/* 为非实时任务指定的 nice 值 */
	s32 sched_nice;

	/* SCHED_FIFO, SCHED_RR */
	/* 为 RT 任务指定的优先级，数值越大表示的优先级越高 */
	u32 sched_priority;

	/* SCHED_DEADLINE */
	/* 为 deadline 任务指定的时间调度参数 */
	u64 sched_runtime;
	u64 sched_deadline;
	u64 sched_period;
};

struct exec_domain;
struct futex_pi_state;
struct robust_list_head;
struct bio_list;
struct fs_struct;
struct perf_event_context;
struct blk_plug;
struct filename;

#define VMACACHE_BITS 2
#define VMACACHE_SIZE (1U << VMACACHE_BITS)
#define VMACACHE_MASK (VMACACHE_SIZE - 1)

/*
 * These are the constant used to fake the fixed-point load-average
 * counting. Some notes:
 *  - 11 bit fractions expand to 22 bits by the multiplies: this gives
 *    a load-average precision of 10 bits integer + 11 bits fractional
 *  - if you want to count load-averages more often, you need more
 *    precision, or rounding will get you. With 2-second counting freq,
 *    the EXP_n values would be 1981, 2034 and 2043 if still using only
 *    11 bit fractions.
 */
extern unsigned long avenrun[];		/* Load averages */
extern void get_avenrun(unsigned long *loads, unsigned long offset, int shift);

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_FREQ	(5*HZ+1)	/* 5 sec intervals, units is tick */
#define EXP_1		1884		/* 1/exp(5sec/1min) as fixed-point */
#define EXP_5		2014		/* 1/exp(5sec/5min) */
#define EXP_15		2037		/* 1/exp(5sec/15min) */

#define CALC_LOAD(load,exp,n) \
	load *= exp; \
	load += n*(FIXED_1-exp); \
	load >>= FSHIFT;

extern unsigned long total_forks;
extern int nr_threads;
DECLARE_PER_CPU(unsigned long, process_counts);
extern int nr_processes(void);
extern unsigned long nr_running(void);
extern bool single_task_running(void);
extern unsigned long nr_iowait(void);
extern unsigned long nr_iowait_cpu(int cpu);
extern void get_iowait_load(unsigned long *nr_waiters, unsigned long *load);

extern void calc_global_load(unsigned long ticks);
extern void update_cpu_load_nohz(void);

extern unsigned long get_parent_ip(unsigned long addr);

extern void dump_cpu_task(int cpu);

struct seq_file;
struct cfs_rq;
struct task_group;
#ifdef CONFIG_SCHED_DEBUG
extern void proc_sched_show_task(struct task_struct *p, struct seq_file *m);
extern void proc_sched_set_task(struct task_struct *p);
extern void
print_cfs_rq(struct seq_file *m, int cpu, struct cfs_rq *cfs_rq);
#endif

/*
 * Task state bitmask. NOTE! These bits are also
 * encoded in fs/proc/array.c: get_task_state().
 *
 * We have two separate sets of flags: task->state
 * is about runnability, while task->exit_state are
 * about the task exiting. Confusing, but this way
 * modifying one set can't modify the other one by
 * mistake.
 */
#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define __TASK_STOPPED		4
#define __TASK_TRACED		8
/* in tsk->exit_state */
#define EXIT_DEAD		16
#define EXIT_ZOMBIE		32
#define EXIT_TRACE		(EXIT_ZOMBIE | EXIT_DEAD)
/* in tsk->state again */
#define TASK_DEAD		64
#define TASK_WAKEKILL		128
#define TASK_WAKING		256
#define TASK_PARKED		512
#define TASK_STATE_MAX		1024

#define TASK_STATE_TO_CHAR_STR "RSDTtXZxKWP"

extern char ___assert_task_state[1 - 2*!!(
		sizeof(TASK_STATE_TO_CHAR_STR)-1 != ilog2(TASK_STATE_MAX)+1)];

/* Convenience macros for the sake of set_task_state */
#define TASK_KILLABLE		(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)
#define TASK_STOPPED		(TASK_WAKEKILL | __TASK_STOPPED)
#define TASK_TRACED		(TASK_WAKEKILL | __TASK_TRACED)

/* Convenience macros for the sake of wake_up */
#define TASK_NORMAL		(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)
#define TASK_ALL		(TASK_NORMAL | __TASK_STOPPED | __TASK_TRACED)

/* get_task_state() */
#define TASK_REPORT		(TASK_RUNNING | TASK_INTERRUPTIBLE | \
				 TASK_UNINTERRUPTIBLE | __TASK_STOPPED | \
				 __TASK_TRACED | EXIT_ZOMBIE | EXIT_DEAD)

#define task_is_traced(task)	((task->state & __TASK_TRACED) != 0)
#define task_is_stopped(task)	((task->state & __TASK_STOPPED) != 0)
#define task_is_stopped_or_traced(task)	\
			((task->state & (__TASK_STOPPED | __TASK_TRACED)) != 0)
#define task_contributes_to_load(task)	\
				((task->state & TASK_UNINTERRUPTIBLE) != 0 && \
				 (task->flags & PF_FROZEN) == 0)

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP

#define __set_task_state(tsk, state_value)			\
	do {							\
		(tsk)->task_state_change = _THIS_IP_;		\
		(tsk)->state = (state_value);			\
	} while (0)
#define set_task_state(tsk, state_value)			\
	do {							\
		(tsk)->task_state_change = _THIS_IP_;		\
		set_mb((tsk)->state, (state_value));		\
	} while (0)

/*
 * set_current_state() includes a barrier so that the write of current->state
 * is correctly serialised wrt the caller's subsequent test of whether to
 * actually sleep:
 *
 *	set_current_state(TASK_UNINTERRUPTIBLE);
 *	if (do_i_need_to_sleep())
 *		schedule();
 *
 * If the caller does not need such serialisation then use __set_current_state()
 */
/*********************************************************************************************************
** 函数名称: __set_current_state
** 功能描述: 设置当前正在运行的任务状态为指定的值，不执行内存屏障操作
** 输	 入: state_value - 指定的任务状态
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __set_current_state(state_value)			\
	do {							\
		current->task_state_change = _THIS_IP_;		\
		current->state = (state_value);			\
	} while (0)

/*********************************************************************************************************
** 函数名称: set_current_state
** 功能描述: 设置当前正在运行的任务状态为指定的值，并执行内存屏障操作
** 输	 入: state_value - 指定的任务状态
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define set_current_state(state_value)				\
	do {							\
		current->task_state_change = _THIS_IP_;		\
		set_mb(current->state, (state_value));		\
	} while (0)

#else

#define __set_task_state(tsk, state_value)		\
	do { (tsk)->state = (state_value); } while (0)
#define set_task_state(tsk, state_value)		\
	set_mb((tsk)->state, (state_value))

/*
 * set_current_state() includes a barrier so that the write of current->state
 * is correctly serialised wrt the caller's subsequent test of whether to
 * actually sleep:
 *
 *	set_current_state(TASK_UNINTERRUPTIBLE);
 *	if (do_i_need_to_sleep())
 *		schedule();
 *
 * If the caller does not need such serialisation then use __set_current_state()
 */
/*********************************************************************************************************
** 函数名称: __set_current_state
** 功能描述: 设置当前正在运行的任务状态为指定的值，不执行内存屏障操作
** 输	 入: state_value - 指定的任务状态
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __set_current_state(state_value)		\
	do { current->state = (state_value); } while (0)

/*********************************************************************************************************
** 函数名称: set_current_state
** 功能描述: 设置当前正在运行的任务状态为指定的值，并执行内存屏障操作
** 输	 入: state_value - 指定的任务状态
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define set_current_state(state_value)			\
	set_mb(current->state, (state_value))

#endif

/* Task command name length */
#define TASK_COMM_LEN 16

#include <linux/spinlock.h>

/*
 * This serializes "schedule()" and also protects
 * the run-queue from deletions/modifications (but
 * _adding_ to the beginning of the run-queue has
 * a separate lock).
 */
extern rwlock_t tasklist_lock;
extern spinlock_t mmlist_lock;

struct task_struct;

#ifdef CONFIG_PROVE_RCU
extern int lockdep_tasklist_lock_is_held(void);
#endif /* #ifdef CONFIG_PROVE_RCU */

extern void sched_init(void);
extern void sched_init_smp(void);
extern asmlinkage void schedule_tail(struct task_struct *prev);
extern void init_idle(struct task_struct *idle, int cpu);
extern void init_idle_bootup_task(struct task_struct *idle);

extern int runqueue_is_locked(int cpu);

#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
extern void nohz_balance_enter_idle(int cpu);
extern void set_cpu_sd_state_idle(void);
extern int get_nohz_timer_target(int pinned);
#else
static inline void nohz_balance_enter_idle(int cpu) { }
static inline void set_cpu_sd_state_idle(void) { }
static inline int get_nohz_timer_target(int pinned)
{
	return smp_processor_id();
}
#endif

/*
 * Only dump TASK_* tasks. (0 for all tasks)
 */
extern void show_state_filter(unsigned long state_filter);

static inline void show_state(void)
{
	show_state_filter(0);
}

extern void show_regs(struct pt_regs *);

/*
 * TASK is a pointer to the task whose backtrace we want to see (or NULL for current
 * task), SP is the stack pointer of the first frame that should be shown in the back
 * trace (or NULL if the entire call-chain of the task should be shown).
 */
extern void show_stack(struct task_struct *task, unsigned long *sp);

extern void cpu_init (void);
extern void trap_init(void);
extern void update_process_times(int user);
extern void scheduler_tick(void);

extern void sched_show_task(struct task_struct *p);

#ifdef CONFIG_LOCKUP_DETECTOR
extern void touch_softlockup_watchdog(void);
extern void touch_softlockup_watchdog_sync(void);
extern void touch_all_softlockup_watchdogs(void);
extern int proc_dowatchdog_thresh(struct ctl_table *table, int write,
				  void __user *buffer,
				  size_t *lenp, loff_t *ppos);
extern unsigned int  softlockup_panic;
void lockup_detector_init(void);
#else
static inline void touch_softlockup_watchdog(void)
{
}
static inline void touch_softlockup_watchdog_sync(void)
{
}
static inline void touch_all_softlockup_watchdogs(void)
{
}
static inline void lockup_detector_init(void)
{
}
#endif

#ifdef CONFIG_DETECT_HUNG_TASK
void reset_hung_task_detector(void);
#else
static inline void reset_hung_task_detector(void)
{
}
#endif

/* Attach to any functions which should be ignored in wchan output. */
#define __sched		__attribute__((__section__(".sched.text")))

/* Linker adds these: start and end of __sched functions */
extern char __sched_text_start[], __sched_text_end[];

/* Is this address in the __sched functions? */
extern int in_sched_functions(unsigned long addr);

#define	MAX_SCHEDULE_TIMEOUT	LONG_MAX
extern signed long schedule_timeout(signed long timeout);
extern signed long schedule_timeout_interruptible(signed long timeout);
extern signed long schedule_timeout_killable(signed long timeout);
extern signed long schedule_timeout_uninterruptible(signed long timeout);
asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);

extern long io_schedule_timeout(long timeout);

static inline void io_schedule(void)
{
	io_schedule_timeout(MAX_SCHEDULE_TIMEOUT);
}

struct nsproxy;
struct user_namespace;

#ifdef CONFIG_MMU
extern void arch_pick_mmap_layout(struct mm_struct *mm);
extern unsigned long
arch_get_unmapped_area(struct file *, unsigned long, unsigned long,
		       unsigned long, unsigned long);
extern unsigned long
arch_get_unmapped_area_topdown(struct file *filp, unsigned long addr,
			  unsigned long len, unsigned long pgoff,
			  unsigned long flags);
#else
static inline void arch_pick_mmap_layout(struct mm_struct *mm) {}
#endif

#define SUID_DUMP_DISABLE	0	/* No setuid dumping */
#define SUID_DUMP_USER		1	/* Dump as user of process */
#define SUID_DUMP_ROOT		2	/* Dump as root */

/* mm flags */

/* for SUID_DUMP_* above */
#define MMF_DUMPABLE_BITS 2
#define MMF_DUMPABLE_MASK ((1 << MMF_DUMPABLE_BITS) - 1)

extern void set_dumpable(struct mm_struct *mm, int value);
/*
 * This returns the actual value of the suid_dumpable flag. For things
 * that are using this for checking for privilege transitions, it must
 * test against SUID_DUMP_USER rather than treating it as a boolean
 * value.
 */
static inline int __get_dumpable(unsigned long mm_flags)
{
	return mm_flags & MMF_DUMPABLE_MASK;
}

static inline int get_dumpable(struct mm_struct *mm)
{
	return __get_dumpable(mm->flags);
}

/* coredump filter bits */
#define MMF_DUMP_ANON_PRIVATE	2
#define MMF_DUMP_ANON_SHARED	3
#define MMF_DUMP_MAPPED_PRIVATE	4
#define MMF_DUMP_MAPPED_SHARED	5
#define MMF_DUMP_ELF_HEADERS	6
#define MMF_DUMP_HUGETLB_PRIVATE 7
#define MMF_DUMP_HUGETLB_SHARED  8

#define MMF_DUMP_FILTER_SHIFT	MMF_DUMPABLE_BITS
#define MMF_DUMP_FILTER_BITS	7
#define MMF_DUMP_FILTER_MASK \
	(((1 << MMF_DUMP_FILTER_BITS) - 1) << MMF_DUMP_FILTER_SHIFT)
#define MMF_DUMP_FILTER_DEFAULT \
	((1 << MMF_DUMP_ANON_PRIVATE) |	(1 << MMF_DUMP_ANON_SHARED) |\
	 (1 << MMF_DUMP_HUGETLB_PRIVATE) | MMF_DUMP_MASK_DEFAULT_ELF)

#ifdef CONFIG_CORE_DUMP_DEFAULT_ELF_HEADERS
# define MMF_DUMP_MASK_DEFAULT_ELF	(1 << MMF_DUMP_ELF_HEADERS)
#else
# define MMF_DUMP_MASK_DEFAULT_ELF	0
#endif
					/* leave room for more dump flags */
#define MMF_VM_MERGEABLE	16	/* KSM may merge identical pages */
#define MMF_VM_HUGEPAGE		17	/* set when VM_HUGEPAGE is set on vma */
#define MMF_EXE_FILE_CHANGED	18	/* see prctl_set_mm_exe_file() */

#define MMF_HAS_UPROBES		19	/* has uprobes */
#define MMF_RECALC_UPROBES	20	/* MMF_HAS_UPROBES can be wrong */

#define MMF_INIT_MASK		(MMF_DUMPABLE_MASK | MMF_DUMP_FILTER_MASK)

struct sighand_struct {
	atomic_t		count;
	struct k_sigaction	action[_NSIG];
	spinlock_t		siglock;
	wait_queue_head_t	signalfd_wqh;
};

struct pacct_struct {
	int			ac_flag;
	long			ac_exitcode;
	unsigned long		ac_mem;
	cputime_t		ac_utime, ac_stime;
	unsigned long		ac_minflt, ac_majflt;
};

struct cpu_itimer {
	cputime_t expires;
	cputime_t incr;
	u32 error;
	u32 incr_error;
};

/**
 * struct cputime - snaphsot of system and user cputime
 * @utime: time spent in user mode
 * @stime: time spent in system mode
 *
 * Gathers a generic snapshot of user and system time.
 */
struct cputime {
	cputime_t utime;
	cputime_t stime;
};

/**
 * struct task_cputime - collected CPU time counts
 * @utime:		time spent in user mode, in &cputime_t units
 * @stime:		time spent in kernel mode, in &cputime_t units
 * @sum_exec_runtime:	total time spent on the CPU, in nanoseconds
 *
 * This is an extension of struct cputime that includes the total runtime
 * spent by the task from the scheduler point of view.
 *
 * As a result, this structure groups together three kinds of CPU time
 * that are tracked for threads and thread groups.  Most things considering
 * CPU time want to group these counts together and treat all three
 * of them in parallel.
 */
struct task_cputime {
	cputime_t utime;
	cputime_t stime;
	unsigned long long sum_exec_runtime;
};
/* Alternate field names when used to cache expirations. */
#define prof_exp	stime
#define virt_exp	utime
#define sched_exp	sum_exec_runtime

#define INIT_CPUTIME	\
	(struct task_cputime) {					\
		.utime = 0,					\
		.stime = 0,					\
		.sum_exec_runtime = 0,				\
	}

#ifdef CONFIG_PREEMPT_COUNT
#define PREEMPT_DISABLED	(1 + PREEMPT_ENABLED)
#else
#define PREEMPT_DISABLED	PREEMPT_ENABLED
#endif

/*
 * Disable preemption until the scheduler is running.
 * Reset by start_kernel()->sched_init()->init_idle().
 *
 * We include PREEMPT_ACTIVE to avoid cond_resched() from working
 * before the scheduler is active -- see should_resched().
 */
#define INIT_PREEMPT_COUNT	(PREEMPT_DISABLED + PREEMPT_ACTIVE)

/**
 * struct thread_group_cputimer - thread group interval timer counts
 * @cputime:		thread group interval timers.
 * @running:		non-zero when there are timers running and
 * 			@cputime receives updates.
 * @lock:		lock for fields in this struct.
 *
 * This structure contains the version of task_cputime, above, that is
 * used for thread group CPU timer calculations.
 */
struct thread_group_cputimer {
	struct task_cputime cputime;
	int running;
	raw_spinlock_t lock;
};

#include <linux/rwsem.h>
struct autogroup;

/*
 * NOTE! "signal_struct" does not have its own
 * locking, because a shared signal_struct always
 * implies a shared sighand_struct, so locking
 * sighand_struct is always a proper superset of
 * the locking of signal_struct.
 */
struct signal_struct {
    /* 当前信号引用计数 */
	atomic_t		sigcnt;
	
	atomic_t		live;
	int			nr_threads;
	struct list_head	thread_head;

	wait_queue_head_t	wait_chldexit;	/* for wait4() */

	/* current thread group signal load-balancing target: */
	struct task_struct	*curr_target;

	/* shared signal handling: */
	struct sigpending	shared_pending;

	/* thread group exit support */
	int			group_exit_code;
	/* overloaded:
	 * - notify group_exit_task when ->count is equal to notify_count
	 * - everyone except group_exit_task is stopped during signal delivery
	 *   of fatal signals, group_exit_task processes the signal.
	 */
	int			notify_count;
	struct task_struct	*group_exit_task;

	/* thread group stop support, overloads group_exit_code too */
	int			group_stop_count;
	unsigned int		flags; /* see SIGNAL_* flags below */

	/*
	 * PR_SET_CHILD_SUBREAPER marks a process, like a service
	 * manager, to re-parent orphan (double-forking) child processes
	 * to this process instead of 'init'. The service manager is
	 * able to receive SIGCHLD signals and is able to investigate
	 * the process until it calls wait(). All children of this
	 * process will inherit a flag if they should look for a
	 * child_subreaper process at exit.
	 */
	unsigned int		is_child_subreaper:1;
	unsigned int		has_child_subreaper:1;

	/* POSIX.1b Interval Timers */
	int			posix_timer_id;
	struct list_head	posix_timers;

	/* ITIMER_REAL timer for the process */
	struct hrtimer real_timer;
	struct pid *leader_pid;
	ktime_t it_real_incr;

	/*
	 * ITIMER_PROF and ITIMER_VIRTUAL timers for the process, we use
	 * CPUCLOCK_PROF and CPUCLOCK_VIRT for indexing array as these
	 * values are defined to 0 and 1 respectively
	 */
	struct cpu_itimer it[2];

	/*
	 * Thread group totals for process CPU timers.
	 * See thread_group_cputimer(), et al, for details.
	 */
	struct thread_group_cputimer cputimer;

	/* Earliest-expiration cache. */
	struct task_cputime cputime_expires;

	struct list_head cpu_timers[3];

	struct pid *tty_old_pgrp;

	/* boolean value for session group leader */
	int leader;

	struct tty_struct *tty; /* NULL if no tty */

#ifdef CONFIG_SCHED_AUTOGROUP
	struct autogroup *autogroup;
#endif
	/*
	 * Cumulative resource counters for dead threads in the group,
	 * and for reaped dead child processes forked by this group.
	 * Live threads maintain their own counters and add to these
	 * in __exit_signal, except for the group leader.
	 */
	seqlock_t stats_lock;
	cputime_t utime, stime, cutime, cstime;
	cputime_t gtime;
	cputime_t cgtime;
#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	struct cputime prev_cputime;
#endif
	unsigned long nvcsw, nivcsw, cnvcsw, cnivcsw;
	unsigned long min_flt, maj_flt, cmin_flt, cmaj_flt;
	unsigned long inblock, oublock, cinblock, coublock;
	unsigned long maxrss, cmaxrss;
	struct task_io_accounting ioac;

	/*
	 * Cumulative ns of schedule CPU time fo dead threads in the
	 * group, not including a zombie group leader, (This only differs
	 * from jiffies_to_ns(utime + stime) if sched_clock uses something
	 * other than jiffies.)
	 */
	unsigned long long sum_sched_runtime;

	/*
	 * We don't bother to synchronize most readers of this at all,
	 * because there is no reader checking a limit that actually needs
	 * to get both rlim_cur and rlim_max atomically, and either one
	 * alone is a single word that can safely be read normally.
	 * getrlimit/setrlimit use task_lock(current->group_leader) to
	 * protect this instead of the siglock, because they really
	 * have no need to disable irqs.
	 */
	struct rlimit rlim[RLIM_NLIMITS];

#ifdef CONFIG_BSD_PROCESS_ACCT
	struct pacct_struct pacct;	/* per-process accounting information */
#endif
#ifdef CONFIG_TASKSTATS
	struct taskstats *stats;
#endif
#ifdef CONFIG_AUDIT
	unsigned audit_tty;
	unsigned audit_tty_log_passwd;
	struct tty_audit_buf *tty_audit_buf;
#endif
#ifdef CONFIG_CGROUPS
	/*
	 * group_rwsem prevents new tasks from entering the threadgroup and
	 * member tasks from exiting,a more specifically, setting of
	 * PF_EXITING.  fork and exit paths are protected with this rwsem
	 * using threadgroup_change_begin/end().  Users which require
	 * threadgroup to remain stable should use threadgroup_[un]lock()
	 * which also takes care of exec path.  Currently, cgroup is the
	 * only user.
	 */
	struct rw_semaphore group_rwsem;
#endif

	oom_flags_t oom_flags;
	short oom_score_adj;		/* OOM kill score adjustment */
	short oom_score_adj_min;	/* OOM kill score adjustment min value.
					 * Only settable by CAP_SYS_RESOURCE. */

	struct mutex cred_guard_mutex;	/* guard against foreign influences on
					 * credential calculations
					 * (notably. ptrace) */
};

/*
 * Bits in flags field of signal_struct.
 */
#define SIGNAL_STOP_STOPPED	0x00000001 /* job control stop in effect */
#define SIGNAL_STOP_CONTINUED	0x00000002 /* SIGCONT since WCONTINUED reap */
#define SIGNAL_GROUP_EXIT	0x00000004 /* group exit in progress */
#define SIGNAL_GROUP_COREDUMP	0x00000008 /* coredump in progress */
/*
 * Pending notifications to parent.
 */
#define SIGNAL_CLD_STOPPED	0x00000010
#define SIGNAL_CLD_CONTINUED	0x00000020
#define SIGNAL_CLD_MASK		(SIGNAL_CLD_STOPPED|SIGNAL_CLD_CONTINUED)

#define SIGNAL_UNKILLABLE	0x00000040 /* for init: ignore fatal signals */

/* If true, all threads except ->group_exit_task have pending SIGKILL */
static inline int signal_group_exit(const struct signal_struct *sig)
{
	return	(sig->flags & SIGNAL_GROUP_EXIT) ||
		(sig->group_exit_task != NULL);
}

/*
 * Some day this will be a full-fledged user tracking system..
 */
struct user_struct {
	atomic_t __count;	/* reference count */
	atomic_t processes;	/* How many processes does this user have? */
	atomic_t sigpending;	/* How many pending signals does this user have? */
#ifdef CONFIG_INOTIFY_USER
	atomic_t inotify_watches; /* How many inotify watches does this user have? */
	atomic_t inotify_devs;	/* How many inotify devs does this user have opened? */
#endif
#ifdef CONFIG_FANOTIFY
	atomic_t fanotify_listeners;
#endif
#ifdef CONFIG_EPOLL
	atomic_long_t epoll_watches; /* The number of file descriptors currently watched */
#endif
#ifdef CONFIG_POSIX_MQUEUE
	/* protected by mq_lock	*/
	unsigned long mq_bytes;	/* How many bytes can be allocated to mqueue? */
#endif
	unsigned long locked_shm; /* How many pages of mlocked shm ? */

#ifdef CONFIG_KEYS
	struct key *uid_keyring;	/* UID specific keyring */
	struct key *session_keyring;	/* UID's default session keyring */
#endif

	/* Hash table maintenance information */
	struct hlist_node uidhash_node;
	kuid_t uid;

#ifdef CONFIG_PERF_EVENTS
	atomic_long_t locked_vm;
#endif
};

extern int uids_sysfs_init(void);

extern struct user_struct *find_user(kuid_t);

extern struct user_struct root_user;
#define INIT_USER (&root_user)


struct backing_dev_info;
struct reclaim_state;

#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
struct sched_info {
	/* cumulative counters */
	unsigned long pcount;	      /* # of times run on this cpu */
	unsigned long long run_delay; /* time spent waiting on a runqueue */

	/* timestamps */
	unsigned long long last_arrival,/* when we last ran on a cpu */
			   last_queued;	/* when we were last queued to run */
};
#endif /* defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT) */

#ifdef CONFIG_TASK_DELAY_ACCT
struct task_delay_info {
	spinlock_t	lock;
	unsigned int	flags;	/* Private per-task flags */

	/* For each stat XXX, add following, aligned appropriately
	 *
	 * struct timespec XXX_start, XXX_end;
	 * u64 XXX_delay;
	 * u32 XXX_count;
	 *
	 * Atomicity of updates to XXX_delay, XXX_count protected by
	 * single lock above (split into XXX_lock if contention is an issue).
	 */

	/*
	 * XXX_count is incremented on every XXX operation, the delay
	 * associated with the operation is added to XXX_delay.
	 * XXX_delay contains the accumulated delay time in nanoseconds.
	 */
	u64 blkio_start;	/* Shared by blkio, swapin */
	u64 blkio_delay;	/* wait for sync block io completion */
	u64 swapin_delay;	/* wait for swapin block io completion */
	u32 blkio_count;	/* total count of the number of sync block */
				/* io operations performed */
	u32 swapin_count;	/* total count of the number of swapin block */
				/* io operations performed */

	u64 freepages_start;
	u64 freepages_delay;	/* wait for memory reclaim */
	u32 freepages_count;	/* total count of memory reclaim */
};
#endif	/* CONFIG_TASK_DELAY_ACCT */

static inline int sched_info_on(void)
{
#ifdef CONFIG_SCHEDSTATS
	return 1;
#elif defined(CONFIG_TASK_DELAY_ACCT)
	extern int delayacct_on;
	return delayacct_on;
#else
	return 0;
#endif
}

/* 表示当前系统支持的 cpu idle 状态，在 smp 负载均衡时使用 */
enum cpu_idle_type {
    /* 表示当前 cpu 进入 idle 状态有一段时间了，是从 CPU_NEWLY_IDLE 状态过度过来的
       即经过一段时间的尝试仍然未能 pull 到可运行的调度实例，表示其他 cpu 可能没有
       发生过载现象，所以我们可以降低 pull 的请求强度 */
	CPU_IDLE,          

	/* 表示当前 cpu 不是 idle 状态，即有 TASK_RUNNING 状态的调度实例，所以不太希望从
	   其他 cpu 上 pull 调度实例 */
	CPU_NOT_IDLE,

	/* 表示当前 cpu 刚刚进入 idle 状态，这时很渴望从其他 cpu 上 pull 一个调度实例来运行 */
	CPU_NEWLY_IDLE,
	
	CPU_MAX_IDLE_TYPES
};

/*
 * Increase resolution of cpu_capacity calculations
 */
/* 在负载计算前乘以这个基准值，在负载计算后除以这个基准值，通过这样的方式
   保证计算精度，详情见 update_cpu_capacity 函数 */
#define SCHED_CAPACITY_SHIFT	10

/* 定义了当前系统使用的 cpu capacity 基准值，用来保证在计算 cpu capacity 时的
   计算精度，在更新 cpu capacity 时使用，详情见 update_cpu_capacity  函数 */
#define SCHED_CAPACITY_SCALE	(1L << SCHED_CAPACITY_SHIFT)  /* 1 << 10 */

/*
 * sched-domains (multiprocessor balancing) declarations:
 */
#ifdef CONFIG_SMP
/* 表示当前调度域是否可以执行负载均衡操作 */
#define SD_LOAD_BALANCE		0x0001	/* Do load balancing on this domain. */

/* 表示当前调度域中的 cpu 在变成 idle 状态时是否执行负载均衡操作，详情见 idle_balance 函数 */
#define SD_BALANCE_NEWIDLE	0x0002	/* Balance when about to become idle */

#define SD_BALANCE_EXEC		0x0004	/* Balance on exec */
#define SD_BALANCE_FORK		0x0008	/* Balance on fork, clone */
#define SD_BALANCE_WAKE		0x0010  /* Balance on wakeup */

#define SD_WAKE_AFFINE		    0x0020	/* Wake task to waking CPU */

#define SD_SHARE_CPUCAPACITY	0x0080	/* Domain members share cpu power */
#define SD_SHARE_POWERDOMAIN	0x0100	/* Domain members share power domain */
#define SD_SHARE_PKG_RESOURCES	0x0200	/* Domain members share cpu pkg resources */

/* 表示当前调度域中同时只能有一个 cpu 执行负载均衡操作，详情见 rebalance_domains 函数 */
#define SD_SERIALIZE		0x0400	/* Only a single load balancing instance */

/* SD_ASYM_PACKING 原则是什么：
   因为在类似 POWER7 核心中，在 cpu id 较低的 cpu 上运行 SMT 将会有更好的性能
   （因为他们共享更少的核心运算单元），所以我们要尽量把处于运行状态的 SMT 线程
   迁移到 cpu id 较小的 cpu 核上，而把处于 idle 状态的 SMT 线程迁移到 cpu id
   较大的 cpu 核上，这样会是系统性能更高（这个函数运行在 idle cpu 上）*/
#define SD_ASYM_PACKING		0x0800  /* Place busy groups earlier in the domain */

#define SD_PREFER_SIBLING	0x1000	/* Prefer to place tasks in a sibling domain */

/* 1. 当前层级的调度域之间的 cpu 有重叠部分，即同一个 cpu 可能存在多个调度域中
   2. 当前调度域包含了多个调度组，详情见 free_sched_domain 函数 */   
#define SD_OVERLAP		0x2000	/* sched_domains of this level overlap */

/* 表示当前调度域可以在 numa node 之间执行负载均衡任务迁移 */
#define SD_NUMA			0x4000	/* cross-node balancing */

#ifdef CONFIG_SCHED_SMT
static inline int cpu_smt_flags(void)
{
	return SD_SHARE_CPUCAPACITY | SD_SHARE_PKG_RESOURCES;
}
#endif

#ifdef CONFIG_SCHED_MC
static inline int cpu_core_flags(void)
{
	return SD_SHARE_PKG_RESOURCES;
}
#endif

#ifdef CONFIG_NUMA
static inline int cpu_numa_flags(void)
{
	return SD_NUMA;
}
#endif

struct sched_domain_attr {
	int relax_domain_level;
};

#define SD_ATTR_INIT	(struct sched_domain_attr) {	\
	.relax_domain_level = -1,			\
}

extern int sched_domain_level_max;

struct sched_group;

/* 每个调度域就是具有相同属性和调度策略的 cpu 集合，一般按照层级从上到下
   可以划分为三层，如下：
                    DIE (SOC 层级)

                    MC  (多核层级)

                    SMT (超线程层级)
                    
   它们在整体上是一个树形结构，如下：

                        DIE domain0
                             |
               /---------------------------\
          MC domain0                   MC domain1
              |                             |
         /------------\               /------------\
   SMT domain0   SMT domain1    SMT domain2   SMT domain3

   domain.groups 数据结构指针关系如下（因为 sched_domain 的类型为 per_cpu，所以每个 cpu 上
   只保存当前 cpu 到调度域树形结构根节点之间的路径信息，我们把所有 cpu 的调度域树形结构信
   息拼接在一起，就是一颗完整的树了）：
   
                                         DIE domain0
	                                 DIE domain0.groups()
					        		          |
			            /---------------------/
			           |
			           v
        'MC domain0.groups(circular list)
                       |                                                    
                  MC domain0                                            
               MC domain0.groups                                    
                       |                                                    
                -------/                                             
               /                                                   
              |          
              v
'SMT domain0.groups'(circular list)
              |                        
         SMT domain0              
      SMT domain0.groups        

                                         DIE domain0
	                                 DIE domain0.groups
					        		          |
			            /---------------------/
			           |
			           v
        'MC domain0.groups'(circular list)
                       |                                                    
                  MC domain0                                           
               MC domain0.groups                                     
                       |                                                    
                       \---------------\                                            
                                        |  
                                        v
                         'SMT domain1.groups'(circular list)      
                                        |                          
                                   SMT domain1                
                                SMT domain1.groups        

                                         DIE domain0
	                                 DIE domain0.groups
					        		          |
			                                  \----------------------------\
			                                                                | 
			                                                                v
                                                             'MC domain1.groups'(circular list)
                                                                            |
                                                                        MC domain1
                                                                     MC domain1.groups
                                                                            |
                                                                     -------/
                                                                    /
                                                                   |
                                                                   v
                                                    'SMT domain2.groups'(circular list)  
                                                                   |                          
                                                              SMT domain2                
                                                          SMT domain2.groups         

                                         DIE domain0
	                                 DIE domain0.groups
					        		          |
			                                  \----------------------------\
			                                                                |  
			                                                                v
                                                              'MC domain1.groups'(circular list)
                                                                            |
                                                                        MC domain1
                                                                     MC domain1.groups
                                                                            |
                                                                            \---------------\
                                                                                             |   
                                                                                             v
                                                                              'SMT domain3.groups'(circular list)
                                                                                             |
                                                                                        SMT domain3
                                                                                     SMT domain3.groups

   domain.parent 数据结构指针关系如下（因为 sched_domain 的类型为 per_cpu，所以每个 cpu 上
   只保存当前 cpu 到调度域树形结构根节点之间的路径信息，我们把所有 cpu 的调度域树形结构信
   息拼接在一起，就是一颗完整的树了）：
	
			                    DIE domain0.parent = NULL
                                            ^
                                            |
                       /--------------------/
                       |                                         
	           MC domain0.parent                         
                       ^                                         
                       |                                         
            /----------/
            |                    
    SMT domain0.parent   

			                    DIE domain0.parent = NULL
                                            ^
                                            |
                       /--------------------/
                       |                                        
	           MC domain0.parent                       
                       ^                                         
                       |                                        
                       \---------\                  
                                 |             
                         SMT domain1.parent

			                    DIE domain0.parent = NULL
                                            ^
                                            |
                                            \--------------------\
                                                                 |
	                                                      MC domain1.parent
                                                                 ^
                                                                 |
                                                       /---------/
                                                       |                    
                                              SMT domain2.parent    

			                    DIE domain0.parent = NULL
                                            ^
                                            |
                                            \--------------------\
                                                                 |
	                                                     MC domain1.parent
                                                                 ^
                                                                 |
                                                                 \---------\
                                                                           |
                                                                   SMT domain3.parent

   domain.child 数据结构指针关系如下（因为 sched_domain 的类型为 per_cpu，所以每个 cpu 上
   只保存当前 cpu 到调度域树形结构根节点之间的路径信息，我们把所有 cpu 的调度域树形结构信
   息拼接在一起，就是一颗完整的树了）：

			                    DIE domain0.child
                                            |
                       /--------------------/
                       |          
                       v
	           MC domain0.child                         
                       |                                         
            /----------/
            |    
            v
    SMT domain0.child = NULL   

			                    DIE domain0.child
                                            |
                       /--------------------/
                       |
                       v
	           MC domain0.child                       
                       |                                        
                       \---------\                  
                                 |     
                                 v
                         SMT domain1.child = NULL

			                    DIE domain0.child
                                            |
                                            \--------------------\
                                                                 |
                                                                 v
	                                                      MC domain1.child
                                                                 |
                                                       /---------/
                                                       |  
                                                       v
                                              SMT domain2.child = NULL    

			                    DIE domain0.child
                                            |
                                            \--------------------\
                                                                 |
                                                                 v
	                                                     MC domain1.child
                                                                 |
                                                                 \---------\
                                                                           |
                                                                           v
                                                                   SMT domain3.child = NULL */
struct sched_domain {
	/* These fields must be setup */
	/* 因为 sched_domain 的类型为 per_cpu，即每个 cpu 上会维护自己的调度域树
	   形结构信息，这些信息只保存了当前 cpu 到调度域树形结构根节点之间的路径
	   信息，所以在当前调度域数据结构中只有一个 child 和一个 parent 指针成员
	   我们把所有 cpu 的调度域树形结构信息拼接在一起，就是一颗完整的树了，详
	   情见 build_sched_domain 函数 */
	struct sched_domain *parent;	/* top domain must be null terminated */
	struct sched_domain *child;	/* bottom domain must be null terminated */

	/* 表示当前调度域中包含的负载均衡调度组，通过单向链表连接在一起，最后一个调度组的
	   next 成员指向 sched_domain.groups，即这是一个环形单向链表，这个调度组挂接在当前
	   调度域包含的 cpu 集中 cpu id 值最小的 cpu 上，详情见 build_sched_groups 函数 */
	struct sched_group *groups;	/* the balancing groups of the domain */
	
	unsigned long min_interval;	/* Minimum balance interval ms */
	unsigned long max_interval;	/* Maximum balance interval ms */

	/* 如果当前 cpu 比较忙，则减少执行负载均衡的次数，详情见 get_sd_balance_interval 函数 */
	unsigned int busy_factor;	/* less balancing by factor if busy */

	unsigned int imbalance_pct;	/* No balance until over watermark */
	unsigned int cache_nice_tries;	/* Leave cache hot tasks for # tries */

	/* 对应着 struct rq 结构体的 cpu_load 数组的索引值，在 smp 负载均衡时使用 */
	unsigned int busy_idx;    /* CPU_NOT_IDLE */
	unsigned int idle_idx;    /* CPU_IDLE */
	unsigned int newidle_idx; /* CPU_NEWLY_IDLE */
	
	unsigned int wake_idx;
	unsigned int forkexec_idx;
	
	unsigned int smt_gain;  /* sd->smt_gain = 1178; ~15%，详情见 sd_init 函数 */

    /* 表示当前调度域是否处于 nohz idle 状态，我们可以在处于这个状态的 cpu 上执行 idle 负载均衡操作 */
	int nohz_idle;		/* NOHZ IDLE status */
	
	int flags;			/* See SD_*，例如 SD_LOAD_BALANCE */
	int level;

	/* Runtime fields. */
	/* 表示当前调度域上一次执行负载均衡操作时的 jiffies 时间值，单位是 ns */
	unsigned long last_balance;	/* init to jiffies. units in jiffies */

	/* 表示当前调度域的负载均衡执行周期时间间隔，单位是 ms */
	unsigned int balance_interval;	/* initialise to 1. units in ms. */

    /* 表示当前调度域执行负载均衡操作连续失败的次数 */
	unsigned int nr_balance_failed; /* initialise to 0 */

	/* idle_balance() stats */
	/* 追踪当前调度域在负载均衡操作过程中，单次消耗的最大时间，单位是 ns，详情见 idle_balance 函数 */
	u64 max_newidle_lb_cost;

    /* 表示当前调度域下一次对 max_newidle_lb_cost 衰减的 jiffies 时间，衰减公式为 
       sd->max_newidle_lb_cost = (sd->max_newidle_lb_cost * 253) / 256; 
	   详情见 rebalance_domains 函数 */
	unsigned long next_decay_max_lb_cost;

#ifdef CONFIG_SCHEDSTATS
	/* load_balance() stats */
	unsigned int lb_count[CPU_MAX_IDLE_TYPES];
	unsigned int lb_failed[CPU_MAX_IDLE_TYPES];
	unsigned int lb_balanced[CPU_MAX_IDLE_TYPES];
	unsigned int lb_imbalance[CPU_MAX_IDLE_TYPES];
	unsigned int lb_gained[CPU_MAX_IDLE_TYPES];
	unsigned int lb_hot_gained[CPU_MAX_IDLE_TYPES];
	unsigned int lb_nobusyg[CPU_MAX_IDLE_TYPES];
	unsigned int lb_nobusyq[CPU_MAX_IDLE_TYPES];

	/* Active load balancing */
	unsigned int alb_count;
	unsigned int alb_failed;
	unsigned int alb_pushed;

	/* SD_BALANCE_EXEC stats */
	unsigned int sbe_count;
	unsigned int sbe_balanced;
	unsigned int sbe_pushed;

	/* SD_BALANCE_FORK stats */
	unsigned int sbf_count;
	unsigned int sbf_balanced;
	unsigned int sbf_pushed;

	/* try_to_wake_up() stats */
	unsigned int ttwu_wake_remote;
	unsigned int ttwu_move_affine;
	unsigned int ttwu_move_balance;
#endif
#ifdef CONFIG_SCHED_DEBUG
	char *name;
#endif
	union {
	    /* *per_cpu_ptr(tl->data.sd, cpu)->private = &tl->data，详情见 sd_init 函数 */
		void *private;		/* used during construction */
		struct rcu_head rcu;	/* used during destruction */
	};

    /* 表示当前调度域的 struct sched_domain.span 数组中有效的 bit 位的位数 */
	unsigned int span_weight;
	
	/*
	 * Span of all CPUs in this domain.
	 *
	 * NOTE: this field is variable length. (Allocated dynamically
	 * by attaching extra space to the end of the structure,
	 * depending on how many CPUs the kernel has booted up with)
	 */
    /* 表示当前调度域内包含的 cpu 的掩码值，详情见 build_sched_domain 函数 */
	unsigned long span[0];
};

/*********************************************************************************************************
** 函数名称: sched_domain_span
** 功能描述: 获取指定的调度域内包含的 cpu 的掩码值
** 输	 入: sd - 指定的调度域指针
** 输	 出: struct cpumask * - cpu 掩码值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct cpumask *sched_domain_span(struct sched_domain *sd)
{
	return to_cpumask(sd->span);
}

extern void partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
				    struct sched_domain_attr *dattr_new);

/* Allocate an array of sched domains, for partition_sched_domains(). */
cpumask_var_t *alloc_sched_domains(unsigned int ndoms);
void free_sched_domains(cpumask_var_t doms[], unsigned int ndoms);

bool cpus_share_cache(int this_cpu, int that_cpu);

typedef const struct cpumask *(*sched_domain_mask_f)(int cpu);
typedef int (*sched_domain_flags_f)(void);

#define SDTL_OVERLAP	0x01

/* 定义了当前系统调度域拓扑结构层级的私有数据结构，用来表示指定的 cpu 在不同的调度域拓扑结构
   层级上调度域结构指针、调度组结构指针以及调度组算计结构指针
   根据这个结构可以看出，在每一个调度域拓扑结构层级上为每个 cpu 都分配了一组 struct sd_data 
   数据，另外因为多个 cpu 可能属于同一个调度组，所以多个 cpu 在同一个调度域拓扑结构层级上可
   能会有相同的 struct sd_data 结构数据，示意图如下：

   sd.a(cpu0 sd)       sd.a(cpu1 sd)       sd.b(cpu2 sd)       sd.b(cpu3 sd)
   sg.a(cpu0 sg)       sg.a(cpu1 sg)       sg.b(cpu2 sg)       sg.b(cpu3 sg)
   sgc.a(cpu0 sgc)     sgc.a(cpu1 sgc)     sgc.b(cpu2 sgc)     sgc.b(cpu3 sgc)

   它们的等效图如下：

         sga.sgc.cpumask=0xFFFFFFFF      sgb.sgc.cpumask=0xFFFFFFFF
           sda.span(cpu0,cpu1)             sdb.span(cpu2,cpu3)
             sda.groups(cpu0)                sdb.groups(cpu2)
                 sd.a                             sd.b
                 sg.a                             sg.b
                 sgc.a                            sgc.b
                   |                                |
            /------------\                   /-------------\
           |              |                 |               |
         cpu0            cpu1              cpu2            cpu3 */
struct sd_data {
    /* 表示每一个 cpu 在当前调度域拓扑结构层级上的调度域结构指针 */
	struct sched_domain **__percpu sd;
	
    /* 表示每一个 cpu 在当前调度域拓扑结构层级上的调度组结构指针 */
	struct sched_group **__percpu sg;
	
    /* 表示每一个 cpu 在当前调度域拓扑结构层级上的调度组算力信息结构指针 */
	struct sched_group_capacity **__percpu sgc;
};

/* 定义了当前系统用来描述调度域拓扑结构的数据结构 */
struct sched_domain_topology_level { 
    /* 用来获取当前调度域层级在指定 cpu 所属区域内包含的 cpu 位图掩码值的函数指针 */
	sched_domain_mask_f mask;

	/* 用来获取当前调度域层级的资源共享属性的函数指针，例如 SD_SHARE_PKG_RESOURCES 属性 */
	sched_domain_flags_f sd_flags;
	
	int		    flags;
	int		    numa_level;

	/* 表示当前调度域拓扑结构的私有数据 */
	struct sd_data      data;

#ifdef CONFIG_SCHED_DEBUG
	char                *name;
#endif
};

extern struct sched_domain_topology_level *sched_domain_topology;

extern void set_sched_topology(struct sched_domain_topology_level *tl);
extern void wake_up_if_idle(int cpu);

#ifdef CONFIG_SCHED_DEBUG
# define SD_INIT_NAME(type)		.name = #type
#else
# define SD_INIT_NAME(type)
#endif

#else /* CONFIG_SMP */

struct sched_domain_attr;

static inline void
partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
			struct sched_domain_attr *dattr_new)
{
}

/*********************************************************************************************************
** 函数名称: cpus_share_cache
** 功能描述: 判断指定的 cpu 是否 cache 亲和性
** 输	 入: this_cpu - 指定的当前 cpu id
**         : that_cpu - 指定的目标 cpu id
** 输	 出: 1 - 共享
**         : 0 - 不共享
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool cpus_share_cache(int this_cpu, int that_cpu)
{
	return true;
}

#endif	/* !CONFIG_SMP */


struct io_context;			/* See blkdev.h */


#ifdef ARCH_HAS_PREFETCH_SWITCH_STACK
extern void prefetch_stack(struct task_struct *t);
#else
static inline void prefetch_stack(struct task_struct *t) { }
#endif

struct audit_context;		/* See audit.c */
struct mempolicy;
struct pipe_inode_info;
struct uts_namespace;

/* 当前系统使用的调度负载权重结构体 */
struct load_weight {
	unsigned long weight; /* 存储了负载权重信息  */
	u32 inv_weight;       /* 存储了负载权重值用于重除的结果 weight * inv_weight = 2^32 = 0x80000000 */
};

/* PELT 负载贡献算法：
   内核计算在一段周期 period 时间内，一个进程处于 runnable 状态的时间来表示该进程对负载的贡献值
   为了统计的精确性，需要计算一个 average 值作为负载贡献值。可以把过去多个 period 周期的负载贡献
   值取一个平均，但是这就带来一个问题，把很久之前的负载贡献加入到当前负载贡献平均值计算中，可能
   会引起很大的误差，因此该算法引入了一个衰减因子来计算该平均值，距离当前时间越久的 period 周期
   对当前的平均负载贡献计算影响越小。
   PELT 把时间分成了 1024us 的序列，在每个 1024us 的周期中，一个调度实体（进程或者进程组）对系统
   负载的贡献可以根据该实体处于 runnable 状态（正在 cpu 上运行或者在队列中等待 cpu 调度运行）的
   时间进行计算。对于过去的负载，我们在计算的时候需要乘一个衰减因子。如果定义 Li 表示在周期 Pi
   中该调度实体的对系统负载贡献，那么一个调度实体对系统负荷的总贡献（load_avg）可以表示为：
   L = L0 + L1*y + L2*y^2 + L3*y^3 + ...

   通过这个公式来看，由于我们是累加各个周期中的负载贡献值，所以一个实体在一个计算周期内的负载可能
   会超过 1024us。使用这样序列让计算非常简单，我们不需要使用数组来记录过去的负荷贡献，只要把上次
   计算得到的总贡献值乘以 y 再加上新的 L0 负荷值就得到了新的贡献值了。内核中通过这种公式计算出
   runnable_avg_sum 和 runnable_avg_period，然后两者 runnable_avg_sum / runnable_avg_period 可以
   作为对系统平均负载贡献的描述。*/
struct sched_avg {
	/*
	 * These sums represent an infinite geometric series and so are bound
	 * above by 1024/(1-y).  Thus we only need a u32 to store them for all
	 * choices of y < 1-2^(-32)*1024.
	 */

	/* runnable_avg_sum - 表示在计算 entity_runnable_avg 时累计的处于运行状态的时间计数值（单位是 1024ns）
	   详情见 __update_entity_runnable_avg 函数
	   runnable_avg_period - 表示在计算 entity_runnable_avg 时累计的统计周期计数值（单位是 1024ns）
	   详情见 __update_entity_runnable_avg 函数 */
	u32 runnable_avg_sum, runnable_avg_period;

	/* 表示上一次更新当前调度实例处于可运行状态时间的负载贡献值时的调度系统时间，用来计算运行时间间隔，单位是 ns */
	u64 last_runnable_update;

	/*
	 * We track migrations using entity decay_count <= 0, on a wake-up
	 * migration we use a negative decay count to track the remote decays
	 * accumulated while sleeping.
	 *
	 * Newly forked tasks are enqueued with se->avg.decay_count == 0, they
	 * are seen by enqueue_entity_load_avg() as a migration with an already
	 * constructed load_avg_contrib.
	 */
	/* 记录上次该调度实体离开 cfs 队列时 cfs 运行队列累计运行时间周期数，每个周期是 1ms
	   如果为 0 表示当前调度实例的负载已经同步衰减到和其所属运行队列相同的阶数，详情见 enqueue_entity_load_avg 函数
	   如果大于 0，则表示当前调度实例上次睡眠时已经衰减的阶数（累计运行时间周期数），详情见 dequeue_entity_load_avg 函数
	   如果小于 0，则表示当前调度实例在任务迁移前，和其所属 cfs 运行队列任务时钟相比，负载还需要再衰减的阶数（衰减后的负载
	   是和所属 cfs 运行队列任务时钟同步的负载），详情见 migrate_task_rq_fair 函数和 enqueue_entity_load_avg 函数 */
	s64 decay_count;
	
	/* [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
            p0            p1           p2
          (now)       (~1ms ago)  (~2ms ago)

       上面的 P0、P1、P2...Pn 表示的是 runnable contrib，即在指定的“统计周期”内
       没经过衰减的负载贡献值

	   load_avg_contrib = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
	    		        = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}] */
    /* 表示当前调度实例过去“时间段”内乘以了权重信息（se->load.weight）并经过衰减后的负载贡献值 */
	unsigned long load_avg_contrib;
};

#ifdef CONFIG_SCHEDSTATS
struct sched_statistics {
	u64			wait_start;                    /* 表示当前调度实例加入运行队列等待运行时的时钟信息 */
	u64			wait_max;                      /* 追踪当前调度实例加入运行队列到实际运行等待时间的最大值 */
	u64			wait_count;                    /* 记录当前调度实例加入运行队列后等待运行的次数 */
	u64			wait_sum;                      /* 统计当前调度实例加入运行队列到实际运行的所有等待时间 */
	
	u64			iowait_count;                  /* 表示当前调度实例的 sched_statistics.iowait_sum 字段中统计的次数 */
 	u64			iowait_sum;                    /* 表示当前调度实例在等待 IO 事件上一共消耗的 cpu 物理时间 */

	u64			sleep_start;                   /* 表示当前调度实例上次开始睡眠时的调度队列时钟，task->state == TASK_INTERRUPTIBLE */
	u64			sleep_max;                     /* 表示当前调度实例睡眠时间最长的一次所睡眠的时间大小 */
	s64			sum_sleep_runtime;             /* 表示当前调度实例一共睡眠的 cpu 物理时间 */

	u64			block_start;                   /* 表示当前调度实例被阻塞时的调度队列时钟，task->state == TASK_UNINTERRUPTIBLE */
	u64			block_max;                     /* 表示当前调度实例被阻塞时间最长的一次所阻塞的时间大小 */
	u64			exec_max;                      /* 表示当前调度实例单次运行时间最长的时间大小 */
	u64			slice_max;

	u64			nr_migrations_cold;
	u64			nr_failed_migrations_affine;
	u64			nr_failed_migrations_running;
	u64			nr_failed_migrations_hot;
	u64			nr_forced_migrations;

	u64			nr_wakeups;
	u64			nr_wakeups_sync;
	u64			nr_wakeups_migrate;
	u64			nr_wakeups_local;
	u64			nr_wakeups_remote;
	u64			nr_wakeups_affine;
	u64			nr_wakeups_affine_attempts;
	u64			nr_wakeups_passive;
	u64			nr_wakeups_idle;
};
#endif

/* 用来抽象调度器中的一个调度实例，这个调度实例可以是线程也可以是一个任务组 */
struct sched_entity {

    /* 当前调度实例的负载权重信息 */
	struct load_weight	load;		/* for load-balancing */

    /* 通过这个红黑树节点把当前调度实例添加到调度器的红黑树上 */	
	struct rb_node		run_node;

    /* 通过链表的方式把属于同一个 cpu 运行队列的所有 cfs 调度实例链接起来 */
	struct list_head	group_node;

	/* 表示当前调度实例是否已经在所属的运行队列上 */
	unsigned int		on_rq;

    /* 表示当前调度实例本次调度开始运行（或者上一次更新运行时统计信息）时的运行队列时钟值，单位是 ns */
	u64			exec_start;

	/* 表示当前调度实例总计运行的 cpu 物理运行时间，单位是 ns */
	u64			sum_exec_runtime;

	/* 表示当前调度实例总计运行的虚拟时间 */
	u64			vruntime;

	/* 表示当前调度实例上一次调度结束时总计运行的 cpu 物理运行时间*/
	u64			prev_sum_exec_runtime;

	/* 表示当前调度实例被迁移的次数 */
	u64			nr_migrations;

#ifdef CONFIG_SCHEDSTATS
    /* 表示当前调度实例的运行时调度统计信息 */
	struct sched_statistics statistics;
#endif

#ifdef CONFIG_FAIR_GROUP_SCHED
    /* 表示当前调度任务组实例在任务组树形结构中的深度 */
	int			depth;

	/* 指向当前任务组调度实例的父任务组调度实例指针，se->parent = parent_tg->se[cpu_id]，
	   因为“根任务组”的 root_task_group->se[cpu_id] = NULL，所以 root_task_group 的所有
	   子任务组调度实例的 child_se->parent = NULL，详情见 init_tg_cfs_entry 函数 */
	struct sched_entity	*parent;
	
	/* rq on which this entity is (to be) queued: */
	/* 指向当前任务组调度实例所在的 cfs 运行队列指针（每 cpu 变量类型），当前任务组实例
	   相当于这个 cfs 运行队列上的一个调度实例，即当前任务组调度实例相当于这个 cfs 运行
	   队列红黑树上的一个节点 */
	struct cfs_rq		*cfs_rq;
	
	/* rq "owned" by this entity/group: */
	/* 如果当前调度实例代表的是一个任务组实例（每 cpu 变量类型），则指向当前任务组拥有的 
	   cfs 运行队列，这个 cfs 运行队列上包含了当前任务组在指定 cpu 上拥有的所有调度实例
	   如果当前调度实例代表的是一个线程，则指向 NULL */
	struct cfs_rq		*my_q;
#endif

#ifdef CONFIG_SMP
	/* Per-entity load-tracking */
    /* 用来存储当前调度实例的系统负载贡献数据信息 */
	struct sched_avg	avg;
#endif
};

struct sched_rt_entity {
	struct list_head run_list;
	unsigned long timeout;
	unsigned long watchdog_stamp;
	unsigned int time_slice;

	struct sched_rt_entity *back;
#ifdef CONFIG_RT_GROUP_SCHED
	struct sched_rt_entity	*parent;
	/* rq on which this entity is (to be) queued: */
	struct rt_rq		*rt_rq;
	/* rq "owned" by this entity/group: */
	struct rt_rq		*my_q;
#endif
};

struct sched_dl_entity {
	struct rb_node	rb_node;

	/*
	 * Original scheduling parameters. Copied here from sched_attr
	 * during sched_setattr(), they will remain the same until
	 * the next sched_setattr().
	 */
	u64 dl_runtime;		/* maximum runtime for each instance	*/
	u64 dl_deadline;	/* relative deadline of each instance	*/
	u64 dl_period;		/* separation of two instances (period) */
	u64 dl_bw;		    /* dl_runtime / dl_deadline		*/

	/*
	 * Actual scheduling parameters. Initialized with the values above,
	 * they are continously updated during task execution. Note that
	 * the remaining runtime could be < 0 in case we are in overrun.
	 */
	s64 runtime;		/* remaining runtime for this instance	*/
	u64 deadline;		/* absolute deadline for this instance	*/
	unsigned int flags;	/* specifying the scheduler behaviour	*/

	/*
	 * Some bool flags:
	 *
	 * @dl_throttled tells if we exhausted the runtime. If so, the
	 * task has to wait for a replenishment to be performed at the
	 * next firing of dl_timer.
	 *
	 * @dl_new tells if a new instance arrived. If so we must
	 * start executing it with full runtime and reset its absolute
	 * deadline;
	 *
	 * @dl_boosted tells if we are boosted due to DI. If so we are
	 * outside bandwidth enforcement mechanism (but only until we
	 * exit the critical section);
	 *
	 * @dl_yielded tells if task gave up the cpu before consuming
	 * all its available runtime during the last job.
	 */
	int dl_throttled, dl_new, dl_boosted, dl_yielded;

	/*
	 * Bandwidth enforcement timer. Each -deadline task has its
	 * own bandwidth to be enforced, thus we need one timer per task.
	 */
	struct hrtimer dl_timer;
};

union rcu_special {
	struct {
		bool blocked;
		bool need_qs;
	} b;
	short s;
};
struct rcu_node;

enum perf_event_task_context {
	perf_invalid_context = -1,
	perf_hw_context = 0,
	perf_sw_context,
	perf_nr_task_contexts,
};

struct task_struct {
	/* 表示当前任务的状态，例如 TASK_RUNNING */
	volatile long state; /* -1 unrunnable, 0 runnable, >0 stopped */

	/* 这个成员指向了内核栈最低地址处，即内核栈中 struct thread_info 结构的起始地址 */
	void *stack;
	
	atomic_t usage;
	unsigned int flags;	/* per process flags, defined below */
	unsigned int ptrace;

#ifdef CONFIG_SMP
	struct llist_node wake_entry;

	/* 表示当前任务是否正在 cpu 上运行 */
	int on_cpu;

    /* wakee - 表示将被唤醒的任务 
	   waker - 表示执行唤醒其他任务函数的任务 */
	
	/* last_wakee - 表示当前任务在唤醒其他任务时，最后一次唤醒的任务指针，详情见 record_wakee 函数 */
	struct task_struct *last_wakee;

	/* wakee_flips - 表示当前任务在指定的时间内唤醒了多少个其他任务，详情见 record_wakee 函数 */
	unsigned long wakee_flips;

	/* wakee_flip_decay_ts - 表示当前任务对唤醒其他任务计数值的衰减时间，详情见 record_wakee 函数 */
	unsigned long wakee_flip_decay_ts;

    /* 在当前任务被唤醒时使用，表示这个任务在睡眠前所属 cpu 的 id 值，详情见 try_to_wake_up 函数 */
	int wake_cpu;
#endif
    /* 表示当前任务的 on_run_queue 状态，例如 TASK_ON_RQ_QUEUED */
	int on_rq;

    /* prio - 表示当前任务的有效优先级，这个优先级可能是动态变化的（比如在 rt_mutex 中优先级
              继承机制动态调整优先级），调度器会根据这个优先级决定调度哪个任务
	   static_prio - 表示当前任务的静态优先级，数值越低优先级越大，[0 - 99] 范围内的值没有实际
	                 意义，[100 139] 分别对应的 nice 数值为 [-20 19]，详情见 set_user_nice 函数
	   normal_prio - 表示当前任务的归一化优先级，是把实时优先级和非实时优先级排列在一起后的
	                 优先级数值，-1 表示 deadline 任务，[0 99] 表示 RT 任务，[100 139] 表示
	                 普通任务，数值越小优先级越高 */
	int prio, static_prio, normal_prio;

	/*  表示当前任务的实时优先级，0 表示当前任务是非实时任务，[1 99] 表示当前任务是
        实时任务，且数值越大优先级越高 */
	unsigned int rt_priority;

	/* 表示当前任务所属调度类指针 */
	const struct sched_class *sched_class;

	/* 表示和当前任务对应的调度实例结构 */
	struct sched_entity se;

	struct sched_rt_entity rt;
#ifdef CONFIG_CGROUP_SCHED
	struct task_group *sched_task_group;
#endif
	struct sched_dl_entity dl;

#ifdef CONFIG_PREEMPT_NOTIFIERS
	/* list of struct preempt_notifier: */
	struct hlist_head preempt_notifiers;
#endif

#ifdef CONFIG_BLK_DEV_IO_TRACE
	unsigned int btrace_seq;
#endif

    /* 表示当前任务使用的调度策略，例如 SCHED_FIFO */
	unsigned int policy;

	int nr_cpus_allowed;

	/* 表示可以运行当前任务的 cpu 位图掩码值 */
	cpumask_t cpus_allowed;

#ifdef CONFIG_PREEMPT_RCU
	int rcu_read_lock_nesting;
	union rcu_special rcu_read_unlock_special;
	struct list_head rcu_node_entry;
#endif /* #ifdef CONFIG_PREEMPT_RCU */
#ifdef CONFIG_PREEMPT_RCU
	struct rcu_node *rcu_blocked_node;
#endif /* #ifdef CONFIG_PREEMPT_RCU */
#ifdef CONFIG_TASKS_RCU
	unsigned long rcu_tasks_nvcsw;
	bool rcu_tasks_holdout;
	struct list_head rcu_tasks_holdout_list;
	int rcu_tasks_idle_cpu;
#endif /* #ifdef CONFIG_TASKS_RCU */

#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
	struct sched_info sched_info;
#endif

	struct list_head tasks;
#ifdef CONFIG_SMP
	struct plist_node pushable_tasks;
	struct rb_node pushable_dl_tasks;
#endif

    /* mm - 表示为当前任务分配的内存结构指针，如果是内核线程则为 NULL
       active_mm - 表示当前任务在运行时实际使用的内存结构指针 */
	struct mm_struct *mm, *active_mm;
#ifdef CONFIG_COMPAT_BRK
	unsigned brk_randomized:1;
#endif

	/* per-thread vma caching */
	/* 表示每一个进程当前有效的 vma cache 序号，这个值和 struct mm_struct
	   结构中的 vmacache_seqnum 相对应，只有当这两个值相等的时候，才表示
	   vmacache 有效，所以我们如果想要 invalid 当前 vmacache，只需要把 
	   mm_struct 结构中的 vmacache_seqnum 加一即可 */
	u32 vmacache_seqnum;
	struct vm_area_struct *vmacache[VMACACHE_SIZE];

#if defined(SPLIT_RSS_COUNTING)
	struct task_rss_stat	rss_stat;
#endif
    /* task state */
	int exit_state;
	int exit_code, exit_signal;
	int pdeath_signal;  /*  The signal sent when the parent dies  */
	unsigned int jobctl;	/* JOBCTL_*, siglock protected */

	/* Used for emulating ABI behavior of previous Linux versions */
	unsigned int personality;

	unsigned in_execve:1;	/* Tell the LSMs that the process is doing an execve */

	/* 表示当前任务正在等待 IO 事件 */
	unsigned in_iowait:1;

	/* Revert to default priority/policy when forking */
	/* 表示当前任务执行 fork 操作时“子”任务恢复到默认调度策略和调度优先级，详情见 sched_fork 函数 */
	unsigned sched_reset_on_fork:1;
	
	unsigned sched_contributes_to_load:1;

#ifdef CONFIG_MEMCG_KMEM
	unsigned memcg_kmem_skip_account:1;
#endif

	unsigned long atomic_flags; /* Flags needing atomic access. */

	struct restart_block restart_block;

	/* 表示当前任务的线程 id */
	pid_t pid;

	/* 表示当前任务的线程组 id，即所在进程的进程 id */
	pid_t tgid;

#ifdef CONFIG_CC_STACKPROTECTOR
	/* Canary value for the -fstack-protector gcc feature */
	unsigned long stack_canary;
#endif
	/*
	 * pointers to (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with
	 * p->real_parent->pid)
	 */
	struct task_struct __rcu *real_parent; /* real parent process */

    /* 当前进程的父进程的 task_struct 结构指针 */
	struct task_struct __rcu *parent; /* recipient of SIGCHLD, wait4() reports */
	/*
	 * children/sibling forms the list of my natural children
	 */
    /* 当前进程的孩子节点链表结构 */
	struct list_head children;	/* list of my children */
	
    /* 当前进程的兄弟节点链表结构 */
	struct list_head sibling;	/* linkage in my parent's children list */

	/* 表示当前线程的线程组组长的任务结构体指针，即进程的第一个线程的任务结构体指针 */
	struct task_struct *group_leader;	/* threadgroup leader */

	/*
	 * ptraced is the list of tasks this task is using ptrace on.
	 * This includes both natural children and PTRACE_ATTACH targets.
	 * p->ptrace_entry is p's link on the p->parent->ptraced list.
	 */
	struct list_head ptraced;
	struct list_head ptrace_entry;

	/* PID/PID hash table linkage. */
	struct pid_link pids[PIDTYPE_MAX];
	
	struct list_head thread_group;
	struct list_head thread_node;

    /* 为 vfork 分配的条件变量，在子进程执行完毕后用来通知父进程时使用 */
	struct completion *vfork_done;		/* for vfork() */
	
	int __user *set_child_tid;		/* CLONE_CHILD_SETTID */
	int __user *clear_child_tid;		/* CLONE_CHILD_CLEARTID */

	cputime_t utime, stime, utimescaled, stimescaled;
	cputime_t gtime;
#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	struct cputime prev_cputime;
#endif
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
	seqlock_t vtime_seqlock;
	unsigned long long vtime_snap;
	enum {
		VTIME_SLEEPING = 0,
		VTIME_USER,
		VTIME_SYS,
	} vtime_snap_whence;
#endif
    /* nvcsw - 表示当前任务“主”动切换上下文的次数
       nivcsw - 表示当前任务“被”动切换上下文的次数 */
	unsigned long nvcsw, nivcsw; /* context switch counts */

	u64 start_time;		/* monotonic time in nsec */
	u64 real_start_time;	/* boot based time in nsec */
    /* mm fault and swap info: this can arguably be seen as either mm-specific or thread-specific */
	unsigned long min_flt, maj_flt;

	struct task_cputime cputime_expires;
	struct list_head cpu_timers[3];

    /* process credentials */
	const struct cred __rcu *real_cred; /* objective and real subjective task
					 * credentials (COW) */
	const struct cred __rcu *cred;	/* effective (overridable) subjective task
					 * credentials (COW) */
	char comm[TASK_COMM_LEN]; /* executable name excluding path
				     - access with [gs]et_task_comm (which lock
				       it with task_lock())
				     - initialized normally by setup_new_exec */
/* file system info */
	int link_count, total_link_count;
#ifdef CONFIG_SYSVIPC
/* ipc stuff */
	struct sysv_sem sysvsem;
	struct sysv_shm sysvshm;
#endif
#ifdef CONFIG_DETECT_HUNG_TASK
/* hung task detection */
	unsigned long last_switch_count;
#endif
/* CPU-specific state of this task */
	struct thread_struct thread;
/* filesystem information */
	struct fs_struct *fs;
/* open file information */
	struct files_struct *files;
/* namespaces */
	struct nsproxy *nsproxy;
/* signal handlers */
	struct signal_struct *signal;
	struct sighand_struct *sighand;

	sigset_t blocked, real_blocked;
	sigset_t saved_sigmask;	/* restored if set_restore_sigmask() was used */
	struct sigpending pending;

	unsigned long sas_ss_sp;
	size_t sas_ss_size;
	int (*notifier)(void *priv);
	void *notifier_data;
	sigset_t *notifier_mask;
	struct callback_head *task_works;

	struct audit_context *audit_context;
#ifdef CONFIG_AUDITSYSCALL
	kuid_t loginuid;
	unsigned int sessionid;
#endif
	struct seccomp seccomp;

    /* Thread group tracking */
   	u32 parent_exec_id;
   	u32 self_exec_id;
	
    /* Protection of (de-)allocation: mm, files, fs, tty, keyrings, mems_allowed,
     * mempolicy */
	spinlock_t alloc_lock;

	/* Protection of the PI(Priority Inheritance) data structures: */
	raw_spinlock_t pi_lock;

#ifdef CONFIG_RT_MUTEXES
	/* PI waiters blocked on a rt_mutex held by this task */
    /* 表示正在等待当前任务持有的 rt_mutex 的所有任务成员组成的红黑树信息 */
	struct rb_root pi_waiters;
	struct rb_node *pi_waiters_leftmost;
	
	/* Deadlock detection and priority inheritance handling */
    /* 表示正在等待当前任务持有的 rt_mutex 的任务成员信息 */
	struct rt_mutex_waiter *pi_blocked_on;
#endif

#ifdef CONFIG_DEBUG_MUTEXES
	/* mutex deadlock detection */
	struct mutex_waiter *blocked_on;
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	unsigned int irq_events;
	unsigned long hardirq_enable_ip;
	unsigned long hardirq_disable_ip;
	unsigned int hardirq_enable_event;
	unsigned int hardirq_disable_event;
	int hardirqs_enabled;
	int hardirq_context;
	unsigned long softirq_disable_ip;
	unsigned long softirq_enable_ip;
	unsigned int softirq_disable_event;
	unsigned int softirq_enable_event;
	int softirqs_enabled;
	int softirq_context;
#endif
#ifdef CONFIG_LOCKDEP
# define MAX_LOCK_DEPTH 48UL
	u64 curr_chain_key;
	int lockdep_depth;
	unsigned int lockdep_recursion;
	struct held_lock held_locks[MAX_LOCK_DEPTH];
	gfp_t lockdep_reclaim_gfp;
#endif

/* journalling filesystem info */
	void *journal_info;

/* stacked block device info */
	struct bio_list *bio_list;

#ifdef CONFIG_BLOCK
/* stack plugging */
	struct blk_plug *plug;
#endif

/* VM state */
	struct reclaim_state *reclaim_state;

	struct backing_dev_info *backing_dev_info;

	struct io_context *io_context;

	unsigned long ptrace_message;
	siginfo_t *last_siginfo; /* For ptrace use.  */
	struct task_io_accounting ioac;
#if defined(CONFIG_TASK_XACCT)
	u64 acct_rss_mem1;	/* accumulated rss usage */
	u64 acct_vm_mem1;	/* accumulated virtual memory usage */
	cputime_t acct_timexpd;	/* stime + utime since last update */
#endif
#ifdef CONFIG_CPUSETS
	nodemask_t mems_allowed;	/* Protected by alloc_lock */
	seqcount_t mems_allowed_seq;	/* Seqence no to catch updates */
	int cpuset_mem_spread_rotor;
	int cpuset_slab_spread_rotor;
#endif
#ifdef CONFIG_CGROUPS
	/* Control Group info protected by css_set_lock */
	struct css_set __rcu *cgroups;
	/* cg_list protected by css_set_lock and tsk->alloc_lock */
	struct list_head cg_list;
#endif
#ifdef CONFIG_FUTEX
	struct robust_list_head __user *robust_list;
#ifdef CONFIG_COMPAT
	struct compat_robust_list_head __user *compat_robust_list;
#endif
	struct list_head pi_state_list;
	struct futex_pi_state *pi_state_cache;
#endif
#ifdef CONFIG_PERF_EVENTS
	struct perf_event_context *perf_event_ctxp[perf_nr_task_contexts];
	struct mutex perf_event_mutex;
	struct list_head perf_event_list;
#endif
#ifdef CONFIG_DEBUG_PREEMPT
	unsigned long preempt_disable_ip;
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *mempolicy;	/* Protected by alloc_lock */
	short il_next;
	short pref_node_fork;
#endif
#ifdef CONFIG_NUMA_BALANCING
	int numa_scan_seq;

    /* 表示当前任务对 numa 内存的扫描周期，在扫描过程中，会执行内存页面迁移操作 */
	unsigned int numa_scan_period;
	
    /* 表示当前任务对 numa 内存的最大扫描周期，在扫描过程中，会执行内存页面迁移操作 */
	unsigned int numa_scan_period_max;

	/* 表示当前任务在运行时优先选择的 node id 值 */
	int numa_preferred_nid;

	unsigned long numa_migrate_retry;
	u64 node_stamp;			/* migration stamp  */

	/* 表示当前任务最后一次执行 task_numa_placement 函数时的运行队列时钟值，在 fork 时
	   初始化为 0，详情见 numa_get_avg_runtime 函数 */
	u64 last_task_numa_placement;

	/* 表示当前任务最后一次执行 task_numa_placement 函数时的总计运行的 cpu 物理
	   运行时间，详情见 numa_get_avg_runtime 函数 */
	u64 last_sum_exec_runtime;

	/* 表示当前任务待处理的 work 链表，详情见 task_tick_numa 函数 */
	struct callback_head numa_work;

	struct list_head numa_entry;

	/* 表示当前任务所属 numa_group 指针，当前系统会把访问共享内存的多个任务放到同一个
	   numa 组内，并且把之前基于单独任务的 muna_pte faults 统计信息上升到基于 numa 组
	   的 numa_group_pte faults 统计
	   
	   那么我们是怎么在发生的 numa_pte faults 中判断是否有多个进程访问同一个物理内存页
	   呢？我们通过在 struct page.flags 中添加了 cpupid 标志位，表示上一次访问这个物理
	   内存页的 cpu 和 pid 信息，我们通过比较当前进程的 cpupid 和 上次记录的 cpupid 就
	   可以判断这个物理内存页是否被多个进程同时访问
	   
	   因为在 struct page.flags 中记录的 cpupid 的 pid 域只有 8 bits，所以这 8 bits 只
	   代表了真实进程 pid 的低八位，所以可能会导致歧义发生，但是为了节省内存这是不可避免的 */
	struct numa_group *numa_group;

	/*
	 * numa_faults is an array split into four regions:
	 * faults_memory, faults_cpu, faults_memory_buffer, faults_cpu_buffer
	 * in this precise order.
	 * 
	 * faults_memory: Exponential decaying average of faults on a per-node
	 * basis. Scheduling placement decisions are made based on these
	 * counts. The values remain static for the duration of a PTE scan.
	 *
	 * faults_cpu: Track the nodes the process was running on when a NUMA
	 * hinting fault was incurred.
	 *
	 * faults_memory_buffer and faults_cpu_buffer: Record faults per node
	 * during the current scan window. When the scan completes, the counts
	 * in faults_memory and faults_cpu decay and these values are copied.
	 *
	 *
	 * numa_faults 是按照指定顺序划分成四个区域的数组指针，划分顺序分别是
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

	/* 按照指定顺序划分成四个 faults 区域的数组指针，存储了当前任务在指定周期内在不同 node 节点上
	   发生过 numa_pte faults 的物理内存页个数，我们通过这些数据可以定位在整个统计周期内当前任务
	   访问内存的布局图，这样我们就可以尝试把当前任务迁移到访问内存最多的 node 节点处来提高性能
	   触发 numa_pte faults 的函数是 task_numa_work */
	unsigned long *numa_faults;

	/* 表示在 task_struct.numa_faults 数组中所有成员的总和 */
	unsigned long total_numa_faults;

	/*
	 * numa_faults_locality tracks if faults recorded during the last
	 * scan window were remote/local or failed to migrate. The task scan
	 * period is adapted based on the locality of the faults with different
	 * weights depending on whether they were shared or private faults
	 */
	/* 用来统计当前任务在指定的扫描周期内发生过的 numa_pte faults 次数，并根据类型
	   分别存储在不同的位置，具体如下：
	   remote numa_pte faults - numa_faults_locality[0]
	   local  numa_pte faults - numa_faults_locality[1]
	   failed to migrate - numa_faults_locality[2]
	   这个数组的数据在每一个 numa 内存扫描周期执行完之后都会清零，详情见 update_task_scan_period */
	unsigned long numa_faults_locality[3];

	unsigned long numa_pages_migrated;
#endif /* CONFIG_NUMA_BALANCING */

	struct rcu_head rcu;

	/*
	 * cache last used pipe for splice
	 */
	struct pipe_inode_info *splice_pipe;

	struct page_frag task_frag;

#ifdef	CONFIG_TASK_DELAY_ACCT
	struct task_delay_info *delays;
#endif
#ifdef CONFIG_FAULT_INJECTION
	int make_it_fail;
#endif
	/*
	 * when (nr_dirtied >= nr_dirtied_pause), it's time to call
	 * balance_dirty_pages() for some dirty throttling pause
	 */
	int nr_dirtied;
	int nr_dirtied_pause;
	unsigned long dirty_paused_when; /* start of a write-and-pause period */

#ifdef CONFIG_LATENCYTOP
	int latency_record_count;
	struct latency_record latency_record[LT_SAVECOUNT];
#endif
	/*
	 * time slack values; these are used to round up poll() and
	 * select() etc timeout values. These are in nanoseconds.
	 */
    /* 表示当前任务使用的高精度定时器抖动值，单位是 ns */
	unsigned long timer_slack_ns;

	unsigned long default_timer_slack_ns;

#ifdef CONFIG_KASAN
	unsigned int kasan_depth;
#endif
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* Index of current stored address in ret_stack */
	int curr_ret_stack;
	/* Stack of return addresses for return function tracing */
	struct ftrace_ret_stack	*ret_stack;
	/* time stamp for last schedule */
	unsigned long long ftrace_timestamp;
	/*
	 * Number of functions that haven't been traced
	 * because of depth overrun.
	 */
	atomic_t trace_overrun;
	/* Pause for the tracing */
	atomic_t tracing_graph_pause;
#endif
#ifdef CONFIG_TRACING
	/* state flags for use by tracers */
	unsigned long trace;
	/* bitmask and counter of trace recursion */
	unsigned long trace_recursion;
#endif /* CONFIG_TRACING */
#ifdef CONFIG_MEMCG
	struct memcg_oom_info {
		struct mem_cgroup *memcg;
		gfp_t gfp_mask;
		int order;
		unsigned int may_oom:1;
	} memcg_oom;
#endif
#ifdef CONFIG_UPROBES
	struct uprobe_task *utask;
#endif
#if defined(CONFIG_BCACHE) || defined(CONFIG_BCACHE_MODULE)
	unsigned int	sequential_io;
	unsigned int	sequential_io_avg;
#endif
#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
	unsigned long	task_state_change;
#endif
};

/* Future-safe accessor for struct task_struct's cpus_allowed. */
/*********************************************************************************************************
** 函数名称: tsk_cpus_allowed
** 功能描述: 获取指定的任务允许运行的 cpu 掩码值
** 输	 入: tsk - 指定的任务指针
** 输	 出: tsk)->cpus_allowed - 允许运行的 cpu 掩码值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define tsk_cpus_allowed(tsk) (&(tsk)->cpus_allowed)

/* TNF = task numa flags */

#define TNF_MIGRATED	0x01

/* 表示指定的物理内存页面是只读页面 */
#define TNF_NO_GROUP	0x02

/* 表示指定的物理内存页面是共享内存页面 */
#define TNF_SHARED	0x04

#define TNF_FAULT_LOCAL	0x08
#define TNF_MIGRATE_FAIL 0x10

#ifdef CONFIG_NUMA_BALANCING
extern void task_numa_fault(int last_node, int node, int pages, int flags);
extern pid_t task_numa_group_id(struct task_struct *p);
extern void set_numabalancing_state(bool enabled);
extern void task_numa_free(struct task_struct *p);
extern bool should_numa_migrate_memory(struct task_struct *p, struct page *page,
					int src_nid, int dst_cpu);
#else
static inline void task_numa_fault(int last_node, int node, int pages,
				   int flags)
{
}
static inline pid_t task_numa_group_id(struct task_struct *p)
{
	return 0;
}
static inline void set_numabalancing_state(bool enabled)
{
}
static inline void task_numa_free(struct task_struct *p)
{
}
static inline bool should_numa_migrate_memory(struct task_struct *p,
				struct page *page, int src_nid, int dst_cpu)
{
	return true;
}
#endif

/*********************************************************************************************************
** 函数名称: task_pid
** 功能描述: 获取指定任务的进程 pid 数据结构指针
** 输	 入: task - 指定的任务指针
** 输	 出: pid * - 获取到的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct pid *task_pid(struct task_struct *task)
{
	return task->pids[PIDTYPE_PID].pid;
}

/*********************************************************************************************************
** 函数名称: task_tgid
** 功能描述: 获取指定任务的“线程组组长”的 pid 数据结构指针，即线程所在进程的 pid 数据结构指针
** 输	 入: task - 指定的任务指针
** 输	 出: pid * - 获取到的线程组组长的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct pid *task_tgid(struct task_struct *task)
{
	return task->group_leader->pids[PIDTYPE_PID].pid;
}

/*
 * Without tasklist or rcu lock it is not safe to dereference
 * the result of task_pgrp/task_session even if task == current,
 * we can race with another thread doing sys_setsid/sys_setpgid.
 */
/*********************************************************************************************************
** 函数名称: task_tgid
** 功能描述: 获取指定任务的“进程组组长”的 pid 数据结构指针
** 输	 入: task - 指定的任务指针
** 输	 出: pid * - 获取到的进程组组长的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct pid *task_pgrp(struct task_struct *task)
{
	return task->group_leader->pids[PIDTYPE_PGID].pid;
}

/*********************************************************************************************************
** 函数名称: task_tgid
** 功能描述: 获取指定任务的“会话组组长”的 pid 数据结构指针
** 输	 入: task - 指定的任务指针
** 输	 出: pid * - 获取到的会话组组长的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct pid *task_session(struct task_struct *task)
{
	return task->group_leader->pids[PIDTYPE_SID].pid;
}

struct pid_namespace;

/*
 * the helpers to get the task's different pids as they are seen
 * from various namespaces
 *
 * task_xid_nr()     : global id, i.e. the id seen from the init namespace;
 * task_xid_vnr()    : virtual id, i.e. the id seen from the pid namespace of
 *                     current.
 * task_xid_nr_ns()  : id seen from the ns specified;
 *
 * set_task_vxid()   : assigns a virtual id to a task;
 *
 * see also pid_nr() etc in include/linux/pid.h
 */
pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type,
			struct pid_namespace *ns);

/*********************************************************************************************************
** 函数名称: task_pid_nr
** 功能描述: 获取指定任务的线程 id
** 输	 入: tsk - 指定的任务指针
** 输	 出: pid_t - 获取到的线程 id 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_pid_nr(struct task_struct *tsk)
{
	return tsk->pid;
}

/*********************************************************************************************************
** 函数名称: __task_pid_nr_ns
** 功能描述: 获取指定任务的 PIDTYPE_PID 类型的 pid 在指定的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
**         : ns - 指定的 pid namespace 结构指针
** 输	 出: nr - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_pid_nr_ns(struct task_struct *tsk,
					struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PID, ns);
}

/*********************************************************************************************************
** 函数名称: task_pid_vnr
** 功能描述: 获取指定任务的 PIDTYPE_PID 类型的 pid 在当前运行任务的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: nr - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_pid_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PID, NULL);
}

/*********************************************************************************************************
** 函数名称: task_tgid_nr
** 功能描述: 获取指定任务的线程组 id
** 输	 入: task - 指定的任务结构指针
** 输	 出: tsk->tgid - 获取到的线程组 id
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_tgid_nr(struct task_struct *tsk)
{
	return tsk->tgid;
}

pid_t task_tgid_nr_ns(struct task_struct *tsk, struct pid_namespace *ns);

/*********************************************************************************************************
** 函数名称: task_tgid_vnr
** 功能描述: 获取指定任务的线程组在当前正在运行的进程的 pid namespace 中的 pid 偏移量
** 输	 入: tsk - 指定的任务结构指针
** 输	 出: pid_t - 获取到的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_tgid_vnr(struct task_struct *tsk)
{
	return pid_vnr(task_tgid(tsk));
}


static inline int pid_alive(const struct task_struct *p);

/*********************************************************************************************************
** 函数名称: task_ppid_nr_ns
** 功能描述: 获取指定任务的进程组组长在指定的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
**         : ns - 指定的 pid namespace 结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_ppid_nr_ns(const struct task_struct *tsk, struct pid_namespace *ns)
{
	pid_t pid = 0;

	rcu_read_lock();
	if (pid_alive(tsk))
		/* 获取指定任务的进程组组长在指定的 pid namespace 中的 pid 偏移量 */
		pid = task_tgid_nr_ns(rcu_dereference(tsk->real_parent), ns);
	rcu_read_unlock();

	return pid;
}

/*********************************************************************************************************
** 函数名称: task_ppid_nr
** 功能描述: 获取指定任务的进程组组长在 init_pid_ns pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_ppid_nr(const struct task_struct *tsk)
{
	return task_ppid_nr_ns(tsk, &init_pid_ns);
}

/*********************************************************************************************************
** 函数名称: task_pgrp_nr_ns
** 功能描述: 获取指定任务的 PIDTYPE_PGID 类型的 pid 在指定的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_pgrp_nr_ns(struct task_struct *tsk,
					struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PGID, ns);
}

/*********************************************************************************************************
** 函数名称: task_pgrp_vnr
** 功能描述: 获取指定任务的 PIDTYPE_PGID 类型的 pid 在当前运行进程的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_pgrp_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PGID, NULL);
}

/*********************************************************************************************************
** 函数名称: task_session_nr_ns
** 功能描述: 获取指定任务的 PIDTYPE_SID 类型的 pid 在指定的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_session_nr_ns(struct task_struct *tsk,
					struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_SID, ns);
}

/*********************************************************************************************************
** 函数名称: task_session_vnr
** 功能描述: 获取指定任务的 PIDTYPE_SID 类型的 pid 在当前运行进程的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_session_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_SID, NULL);
}

/* obsolete, do not use */
/*********************************************************************************************************
** 函数名称: task_pgrp_nr
** 功能描述: 获取指定任务的 PIDTYPE_PGID 类型的 pid 在 init_pid_ns pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
** 输	 出: pid_t - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline pid_t task_pgrp_nr(struct task_struct *tsk)
{
	return task_pgrp_nr_ns(tsk, &init_pid_ns);
}

/**
 * pid_alive - check that a task structure is not stale
 * @p: Task structure to be checked.
 *
 * Test if a process is not yet dead (at most zombie state)
 * If pid_alive fails, then pointers within the task structure
 * can be stale and must not be dereferenced.
 *
 * Return: 1 if the process is alive. 0 otherwise.
 */
/*********************************************************************************************************
** 函数名称: pid_alive
** 功能描述: 判断指定任务的进程 id 是否有效
** 输	 入: p - 指定的任务结构指针
** 输	 出: 1 - 有效
**         : 0 - 无效
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int pid_alive(const struct task_struct *p)
{
	return p->pids[PIDTYPE_PID].pid != NULL;
}

/**
 * is_global_init - check if a task structure is init
 * @tsk: Task structure to be checked.
 *
 * Check if a task structure is the first user space task the kernel created.
 *
 * Return: 1 if the task structure is init. 0 otherwise.
 */
/*********************************************************************************************************
** 函数名称: is_global_init
** 功能描述: 判断指定的任务是否是系统 1 号进程
** 输	 入: tsk - 指定的任务结构指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int is_global_init(struct task_struct *tsk)
{
	return tsk->pid == 1;
}

extern struct pid *cad_pid;

extern void free_task(struct task_struct *tsk);

/* 增加指定的 task_struct 的引用计数 */
#define get_task_struct(tsk) do { atomic_inc(&(tsk)->usage); } while(0)

extern void __put_task_struct(struct task_struct *t);

/*********************************************************************************************************
** 函数名称: free_task
** 功能描述: 尝试释放指定的 task struct 结构及其占用的资源
** 输	 入: tsk - 指定的 task struct 结构
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void put_task_struct(struct task_struct *t)
{
	if (atomic_dec_and_test(&t->usage))
		__put_task_struct(t);
}

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
extern void task_cputime(struct task_struct *t,
			 cputime_t *utime, cputime_t *stime);
extern void task_cputime_scaled(struct task_struct *t,
				cputime_t *utimescaled, cputime_t *stimescaled);
extern cputime_t task_gtime(struct task_struct *t);
#else
static inline void task_cputime(struct task_struct *t,
				cputime_t *utime, cputime_t *stime)
{
	if (utime)
		*utime = t->utime;
	if (stime)
		*stime = t->stime;
}

static inline void task_cputime_scaled(struct task_struct *t,
				       cputime_t *utimescaled,
				       cputime_t *stimescaled)
{
	if (utimescaled)
		*utimescaled = t->utimescaled;
	if (stimescaled)
		*stimescaled = t->stimescaled;
}

static inline cputime_t task_gtime(struct task_struct *t)
{
	return t->gtime;
}
#endif
extern void task_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st);
extern void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut, cputime_t *st);

/*
 * Per process flags
 */
#define PF_EXITING	0x00000004	/* getting shut down */
#define PF_EXITPIDONE	0x00000008	/* pi exit done on shut down */
#define PF_VCPU		0x00000010	/* I'm a virtual CPU */
#define PF_WQ_WORKER	0x00000020	/* I'm a workqueue worker */
#define PF_FORKNOEXEC	0x00000040	/* forked but didn't exec */
#define PF_MCE_PROCESS  0x00000080      /* process policy on mce errors */
#define PF_SUPERPRIV	0x00000100	/* used super-user privileges */
#define PF_DUMPCORE	0x00000200	/* dumped core */
#define PF_SIGNALED	0x00000400	/* killed by a signal */
#define PF_MEMALLOC	0x00000800	/* Allocating memory */
#define PF_NPROC_EXCEEDED 0x00001000	/* set_user noticed that RLIMIT_NPROC was exceeded */
#define PF_USED_MATH	0x00002000	/* if unset the fpu must be initialized before use */
#define PF_USED_ASYNC	0x00004000	/* used async_schedule*(), used by module init */
#define PF_NOFREEZE	0x00008000	/* this thread should not be frozen */
#define PF_FROZEN	0x00010000	/* frozen for system suspend */
#define PF_FSTRANS	0x00020000	/* inside a filesystem transaction */
#define PF_KSWAPD	0x00040000	/* I am kswapd */
#define PF_MEMALLOC_NOIO 0x00080000	/* Allocating memory without IO involved */
#define PF_LESS_THROTTLE 0x00100000	/* Throttle me less: I clean memory */
#define PF_KTHREAD	0x00200000	/* I am a kernel thread */
#define PF_RANDOMIZE	0x00400000	/* randomize virtual address space */
#define PF_SWAPWRITE	0x00800000	/* Allowed to write to swap */
#define PF_NO_SETAFFINITY 0x04000000	/* Userland is not allowed to meddle with cpus_allowed */
#define PF_MCE_EARLY    0x08000000      /* Early kill for mce process policy */
#define PF_MUTEX_TESTER	0x20000000	/* Thread belongs to the rt mutex tester */
#define PF_FREEZER_SKIP	0x40000000	/* Freezer should not count it as freezable */
#define PF_SUSPEND_TASK 0x80000000      /* this thread called freeze_processes and should not be frozen */

/*
 * Only the _current_ task can read/write to tsk->flags, but other
 * tasks can access tsk->flags in readonly mode for example
 * with tsk_used_math (like during threaded core dumping).
 * There is however an exception to this rule during ptrace
 * or during fork: the ptracer task is allowed to write to the
 * child->flags of its traced child (same goes for fork, the parent
 * can write to the child->flags), because we're guaranteed the
 * child is not running and in turn not changing child->flags
 * at the same time the parent does it.
 */
#define clear_stopped_child_used_math(child) do { (child)->flags &= ~PF_USED_MATH; } while (0)
#define set_stopped_child_used_math(child) do { (child)->flags |= PF_USED_MATH; } while (0)
#define clear_used_math() clear_stopped_child_used_math(current)
#define set_used_math() set_stopped_child_used_math(current)
#define conditional_stopped_child_used_math(condition, child) \
	do { (child)->flags &= ~PF_USED_MATH, (child)->flags |= (condition) ? PF_USED_MATH : 0; } while (0)
#define conditional_used_math(condition) \
	conditional_stopped_child_used_math(condition, current)
#define copy_to_stopped_child_used_math(child) \
	do { (child)->flags &= ~PF_USED_MATH, (child)->flags |= current->flags & PF_USED_MATH; } while (0)
/* NOTE: this will return 0 or PF_USED_MATH, it will never return 1 */
#define tsk_used_math(p) ((p)->flags & PF_USED_MATH)
#define used_math() tsk_used_math(current)

/* __GFP_IO isn't allowed if PF_MEMALLOC_NOIO is set in current->flags
 * __GFP_FS is also cleared as it implies __GFP_IO.
 */
static inline gfp_t memalloc_noio_flags(gfp_t flags)
{
	if (unlikely(current->flags & PF_MEMALLOC_NOIO))
		flags &= ~(__GFP_IO | __GFP_FS);
	return flags;
}

static inline unsigned int memalloc_noio_save(void)
{
	unsigned int flags = current->flags & PF_MEMALLOC_NOIO;
	current->flags |= PF_MEMALLOC_NOIO;
	return flags;
}

static inline void memalloc_noio_restore(unsigned int flags)
{
	current->flags = (current->flags & ~PF_MEMALLOC_NOIO) | flags;
}

/* Per-process atomic flags. */
#define PFA_NO_NEW_PRIVS 0	/* May not gain new privileges. */
#define PFA_SPREAD_PAGE  1      /* Spread page cache over cpuset */
#define PFA_SPREAD_SLAB  2      /* Spread some slab caches over cpuset */


#define TASK_PFA_TEST(name, func)					\
	static inline bool task_##func(struct task_struct *p)		\
	{ return test_bit(PFA_##name, &p->atomic_flags); }
#define TASK_PFA_SET(name, func)					\
	static inline void task_set_##func(struct task_struct *p)	\
	{ set_bit(PFA_##name, &p->atomic_flags); }
#define TASK_PFA_CLEAR(name, func)					\
	static inline void task_clear_##func(struct task_struct *p)	\
	{ clear_bit(PFA_##name, &p->atomic_flags); }

TASK_PFA_TEST(NO_NEW_PRIVS, no_new_privs)
TASK_PFA_SET(NO_NEW_PRIVS, no_new_privs)

TASK_PFA_TEST(SPREAD_PAGE, spread_page)
TASK_PFA_SET(SPREAD_PAGE, spread_page)
TASK_PFA_CLEAR(SPREAD_PAGE, spread_page)

TASK_PFA_TEST(SPREAD_SLAB, spread_slab)
TASK_PFA_SET(SPREAD_SLAB, spread_slab)
TASK_PFA_CLEAR(SPREAD_SLAB, spread_slab)

/*
 * task->jobctl flags
 */
#define JOBCTL_STOP_SIGMASK	0xffff	/* signr of the last group stop */

#define JOBCTL_STOP_DEQUEUED_BIT 16	/* stop signal dequeued */
#define JOBCTL_STOP_PENDING_BIT	17	/* task should stop for group stop */
#define JOBCTL_STOP_CONSUME_BIT	18	/* consume group stop count */
#define JOBCTL_TRAP_STOP_BIT	19	/* trap for STOP */
#define JOBCTL_TRAP_NOTIFY_BIT	20	/* trap for NOTIFY */
#define JOBCTL_TRAPPING_BIT	21	/* switching to TRACED */
#define JOBCTL_LISTENING_BIT	22	/* ptracer is listening for events */

#define JOBCTL_STOP_DEQUEUED	(1 << JOBCTL_STOP_DEQUEUED_BIT)
#define JOBCTL_STOP_PENDING	(1 << JOBCTL_STOP_PENDING_BIT)
#define JOBCTL_STOP_CONSUME	(1 << JOBCTL_STOP_CONSUME_BIT)
#define JOBCTL_TRAP_STOP	(1 << JOBCTL_TRAP_STOP_BIT)
#define JOBCTL_TRAP_NOTIFY	(1 << JOBCTL_TRAP_NOTIFY_BIT)
#define JOBCTL_TRAPPING		(1 << JOBCTL_TRAPPING_BIT)
#define JOBCTL_LISTENING	(1 << JOBCTL_LISTENING_BIT)

#define JOBCTL_TRAP_MASK	(JOBCTL_TRAP_STOP | JOBCTL_TRAP_NOTIFY)
#define JOBCTL_PENDING_MASK	(JOBCTL_STOP_PENDING | JOBCTL_TRAP_MASK)

extern bool task_set_jobctl_pending(struct task_struct *task,
				    unsigned int mask);
extern void task_clear_jobctl_trapping(struct task_struct *task);
extern void task_clear_jobctl_pending(struct task_struct *task,
				      unsigned int mask);

static inline void rcu_copy_process(struct task_struct *p)
{
#ifdef CONFIG_PREEMPT_RCU
	p->rcu_read_lock_nesting = 0;
	p->rcu_read_unlock_special.s = 0;
	p->rcu_blocked_node = NULL;
	INIT_LIST_HEAD(&p->rcu_node_entry);
#endif /* #ifdef CONFIG_PREEMPT_RCU */
#ifdef CONFIG_TASKS_RCU
	p->rcu_tasks_holdout = false;
	INIT_LIST_HEAD(&p->rcu_tasks_holdout_list);
	p->rcu_tasks_idle_cpu = -1;
#endif /* #ifdef CONFIG_TASKS_RCU */
}

static inline void tsk_restore_flags(struct task_struct *task,
				unsigned long orig_flags, unsigned long flags)
{
	task->flags &= ~flags;
	task->flags |= orig_flags & flags;
}

extern int cpuset_cpumask_can_shrink(const struct cpumask *cur,
				     const struct cpumask *trial);
extern int task_can_attach(struct task_struct *p,
			   const struct cpumask *cs_cpus_allowed);
#ifdef CONFIG_SMP
extern void do_set_cpus_allowed(struct task_struct *p,
			       const struct cpumask *new_mask);

extern int set_cpus_allowed_ptr(struct task_struct *p,
				const struct cpumask *new_mask);
#else
/*********************************************************************************************************
** 函数名称: do_set_cpus_allowed
** 功能描述: 设置指定的任务的 cpus_allowed 字段值
** 输	 入: p - 指定的 task_struct 结构指针
**         : new_mask - 指定的新位图掩码值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void do_set_cpus_allowed(struct task_struct *p,
				      const struct cpumask *new_mask)
{
}
static inline int set_cpus_allowed_ptr(struct task_struct *p,
				       const struct cpumask *new_mask)
{
	if (!cpumask_test_cpu(0, new_mask))
		return -EINVAL;
	return 0;
}
#endif

#ifdef CONFIG_NO_HZ_COMMON
void calc_load_enter_idle(void);
void calc_load_exit_idle(void);
#else
static inline void calc_load_enter_idle(void) { }
static inline void calc_load_exit_idle(void) { }
#endif /* CONFIG_NO_HZ_COMMON */

#ifndef CONFIG_CPUMASK_OFFSTACK
static inline int set_cpus_allowed(struct task_struct *p, cpumask_t new_mask)
{
	return set_cpus_allowed_ptr(p, &new_mask);
}
#endif

/*
 * Do not use outside of architecture code which knows its limitations.
 *
 * sched_clock() has no promise of monotonicity or bounded drift between
 * CPUs, use (which you should not) requires disabling IRQs.
 *
 * Please use one of the three interfaces below.
 */
extern unsigned long long notrace sched_clock(void);
/*
 * See the comment in kernel/sched/clock.c
 */
extern u64 cpu_clock(int cpu);
extern u64 local_clock(void);
extern u64 running_clock(void);
extern u64 sched_clock_cpu(int cpu);


extern void sched_clock_init(void);

#ifndef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
static inline void sched_clock_tick(void)
{
}

static inline void sched_clock_idle_sleep_event(void)
{
}

static inline void sched_clock_idle_wakeup_event(u64 delta_ns)
{
}
#else
/*
 * Architectures can set this to 1 if they have specified
 * CONFIG_HAVE_UNSTABLE_SCHED_CLOCK in their arch Kconfig,
 * but then during bootup it turns out that sched_clock()
 * is reliable after all:
 */
extern int sched_clock_stable(void);
extern void set_sched_clock_stable(void);
extern void clear_sched_clock_stable(void);

extern void sched_clock_tick(void);
extern void sched_clock_idle_sleep_event(void);
extern void sched_clock_idle_wakeup_event(u64 delta_ns);
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
/*
 * An i/f to runtime opt-in for irq time accounting based off of sched_clock.
 * The reason for this explicit opt-in is not to have perf penalty with
 * slow sched_clocks.
 */
extern void enable_sched_clock_irqtime(void);
extern void disable_sched_clock_irqtime(void);
#else
static inline void enable_sched_clock_irqtime(void) {}
static inline void disable_sched_clock_irqtime(void) {}
#endif

extern unsigned long long
task_sched_runtime(struct task_struct *task);

/* sched_exec is called by processes performing an exec */
#ifdef CONFIG_SMP
extern void sched_exec(void);
#else
#define sched_exec()   {}
#endif

extern void sched_clock_idle_sleep_event(void);
extern void sched_clock_idle_wakeup_event(u64 delta_ns);

#ifdef CONFIG_HOTPLUG_CPU
extern void idle_task_exit(void);
#else
static inline void idle_task_exit(void) {}
#endif

#if defined(CONFIG_NO_HZ_COMMON) && defined(CONFIG_SMP)
extern void wake_up_nohz_cpu(int cpu);
#else
static inline void wake_up_nohz_cpu(int cpu) { }
#endif

#ifdef CONFIG_NO_HZ_FULL
extern bool sched_can_stop_tick(void);
extern u64 scheduler_tick_max_deferment(void);
#else
static inline bool sched_can_stop_tick(void) { return false; }
#endif

#ifdef CONFIG_SCHED_AUTOGROUP
extern void sched_autogroup_create_attach(struct task_struct *p);
extern void sched_autogroup_detach(struct task_struct *p);
extern void sched_autogroup_fork(struct signal_struct *sig);
extern void sched_autogroup_exit(struct signal_struct *sig);
#ifdef CONFIG_PROC_FS
extern void proc_sched_autogroup_show_task(struct task_struct *p, struct seq_file *m);
extern int proc_sched_autogroup_set_nice(struct task_struct *p, int nice);
#endif
#else
static inline void sched_autogroup_create_attach(struct task_struct *p) { }
static inline void sched_autogroup_detach(struct task_struct *p) { }
static inline void sched_autogroup_fork(struct signal_struct *sig) { }
static inline void sched_autogroup_exit(struct signal_struct *sig) { }
#endif

extern int yield_to(struct task_struct *p, bool preempt);
extern void set_user_nice(struct task_struct *p, long nice);
extern int task_prio(const struct task_struct *p);
/**
 * task_nice - return the nice value of a given task.
 * @p: the task in question.
 *
 * Return: The nice value [ -20 ... 0 ... 19 ].
 */
/*********************************************************************************************************
** 函数名称: task_nice
** 功能描述: 获取指定任务的 nice 值
** 输	 入: p - 指定的任务指针
** 输	 出: int - nice 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_nice(const struct task_struct *p)
{
	return PRIO_TO_NICE((p)->static_prio);
}
extern int can_nice(const struct task_struct *p, const int nice);
extern int task_curr(const struct task_struct *p);
extern int idle_cpu(int cpu);
extern int sched_setscheduler(struct task_struct *, int,
			      const struct sched_param *);
extern int sched_setscheduler_nocheck(struct task_struct *, int,
				      const struct sched_param *);
extern int sched_setattr(struct task_struct *,
			 const struct sched_attr *);
extern struct task_struct *idle_task(int cpu);
/**
 * is_idle_task - is the specified task an idle task?
 * @p: the task in question.
 *
 * Return: 1 if @p is an idle task. 0 otherwise.
 */
/*********************************************************************************************************
** 函数名称: is_idle_task
** 功能描述: 判断指定的进程是否是 idle 进程
** 输	 入: p - 指定的进程指针
** 输	 出: 1 - 是 idle 进程
**         : 0 - 不是 idle 进程
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool is_idle_task(const struct task_struct *p)
{
	return p->pid == 0;
}
extern struct task_struct *curr_task(int cpu);
extern void set_curr_task(int cpu, struct task_struct *p);

void yield(void);

/*
 * The default (Linux) execution domain.
 */
extern struct exec_domain	default_exec_domain;

/* 通过联合体方式定义系统内核栈数据排列结构，低起始地址存储线程信息
   高起始地址存储内核堆栈信息，增长方向为自上而下 */
union thread_union {
	struct thread_info thread_info;
	unsigned long stack[THREAD_SIZE/sizeof(long)];
};

#ifndef __HAVE_ARCH_KSTACK_END
static inline int kstack_end(void *addr)
{
	/* Reliable end of stack detection:
	 * Some APM bios versions misalign the stack
	 */
	return !(((unsigned long)addr+sizeof(void*)-1) & (THREAD_SIZE-sizeof(void*)));
}
#endif

extern union thread_union init_thread_union;
extern struct task_struct init_task;

extern struct   mm_struct init_mm;

extern struct pid_namespace init_pid_ns;

/*
 * find a task by one of its numerical ids
 *
 * find_task_by_pid_ns():
 *      finds a task by its pid in the specified namespace
 * find_task_by_vpid():
 *      finds a task by its virtual pid
 *
 * see also find_vpid() etc in include/linux/pid.h
 */

extern struct task_struct *find_task_by_vpid(pid_t nr);
extern struct task_struct *find_task_by_pid_ns(pid_t nr,
		struct pid_namespace *ns);

/* per-UID process charging. */
extern struct user_struct * alloc_uid(kuid_t);
static inline struct user_struct *get_uid(struct user_struct *u)
{
	atomic_inc(&u->__count);
	return u;
}
extern void free_uid(struct user_struct *);

#include <asm/current.h>

extern void xtime_update(unsigned long ticks);

extern int wake_up_state(struct task_struct *tsk, unsigned int state);
extern int wake_up_process(struct task_struct *tsk);
extern void wake_up_new_task(struct task_struct *tsk);
#ifdef CONFIG_SMP
 extern void kick_process(struct task_struct *tsk);
#else
 static inline void kick_process(struct task_struct *tsk) { }
#endif
extern int sched_fork(unsigned long clone_flags, struct task_struct *p);
extern void sched_dead(struct task_struct *p);

extern void proc_caches_init(void);
extern void flush_signals(struct task_struct *);
extern void __flush_signals(struct task_struct *);
extern void ignore_signals(struct task_struct *);
extern void flush_signal_handlers(struct task_struct *, int force_default);
extern int dequeue_signal(struct task_struct *tsk, sigset_t *mask, siginfo_t *info);

static inline int dequeue_signal_lock(struct task_struct *tsk, sigset_t *mask, siginfo_t *info)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&tsk->sighand->siglock, flags);
	ret = dequeue_signal(tsk, mask, info);
	spin_unlock_irqrestore(&tsk->sighand->siglock, flags);

	return ret;
}

extern void block_all_signals(int (*notifier)(void *priv), void *priv,
			      sigset_t *mask);
extern void unblock_all_signals(void);
extern void release_task(struct task_struct * p);
extern int send_sig_info(int, struct siginfo *, struct task_struct *);
extern int force_sigsegv(int, struct task_struct *);
extern int force_sig_info(int, struct siginfo *, struct task_struct *);
extern int __kill_pgrp_info(int sig, struct siginfo *info, struct pid *pgrp);
extern int kill_pid_info(int sig, struct siginfo *info, struct pid *pid);
extern int kill_pid_info_as_cred(int, struct siginfo *, struct pid *,
				const struct cred *, u32);
extern int kill_pgrp(struct pid *pid, int sig, int priv);
extern int kill_pid(struct pid *pid, int sig, int priv);
extern int kill_proc_info(int, struct siginfo *, pid_t);
extern __must_check bool do_notify_parent(struct task_struct *, int);
extern void __wake_up_parent(struct task_struct *p, struct task_struct *parent);
extern void force_sig(int, struct task_struct *);
extern int send_sig(int, struct task_struct *, int);
extern int zap_other_threads(struct task_struct *p);
extern struct sigqueue *sigqueue_alloc(void);
extern void sigqueue_free(struct sigqueue *);
extern int send_sigqueue(struct sigqueue *,  struct task_struct *, int group);
extern int do_sigaction(int, struct k_sigaction *, struct k_sigaction *);

static inline void restore_saved_sigmask(void)
{
	if (test_and_clear_restore_sigmask())
		__set_current_blocked(&current->saved_sigmask);
}

static inline sigset_t *sigmask_to_save(void)
{
	sigset_t *res = &current->blocked;
	if (unlikely(test_restore_sigmask()))
		res = &current->saved_sigmask;
	return res;
}

static inline int kill_cad_pid(int sig, int priv)
{
	return kill_pid(cad_pid, sig, priv);
}

/* These can be the second arg to send_sig_info/send_group_sig_info.  */
#define SEND_SIG_NOINFO ((struct siginfo *) 0)
#define SEND_SIG_PRIV	((struct siginfo *) 1)
#define SEND_SIG_FORCED	((struct siginfo *) 2)

/*
 * True if we are on the alternate signal stack.
 */
static inline int on_sig_stack(unsigned long sp)
{
#ifdef CONFIG_STACK_GROWSUP
	return sp >= current->sas_ss_sp &&
		sp - current->sas_ss_sp < current->sas_ss_size;
#else
	return sp > current->sas_ss_sp &&
		sp - current->sas_ss_sp <= current->sas_ss_size;
#endif
}

static inline int sas_ss_flags(unsigned long sp)
{
	if (!current->sas_ss_size)
		return SS_DISABLE;

	return on_sig_stack(sp) ? SS_ONSTACK : 0;
}

static inline unsigned long sigsp(unsigned long sp, struct ksignal *ksig)
{
	if (unlikely((ksig->ka.sa.sa_flags & SA_ONSTACK)) && ! sas_ss_flags(sp))
#ifdef CONFIG_STACK_GROWSUP
		return current->sas_ss_sp;
#else
		return current->sas_ss_sp + current->sas_ss_size;
#endif
	return sp;
}

/*
 * Routines for handling mm_structs
 */
extern struct mm_struct * mm_alloc(void);

/* mmdrop drops the mm and the page tables */
extern void __mmdrop(struct mm_struct *);
/*********************************************************************************************************
** 函数名称: mmdrop
** 功能描述: 尝试释放指定的 mm_struct 结构
** 输	 入: mm - 指定的 mm_struct 数据结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void mmdrop(struct mm_struct * mm)
{
	if (unlikely(atomic_dec_and_test(&mm->mm_count)))
		__mmdrop(mm);
}

/* mmput gets rid of the mappings and all user-space */
extern void mmput(struct mm_struct *);
/* Grab a reference to a task's mm, if it is not already going away */
extern struct mm_struct *get_task_mm(struct task_struct *task);
/*
 * Grab a reference to a task's mm, if it is not already going away
 * and ptrace_may_access with the mode parameter passed to it
 * succeeds.
 */
extern struct mm_struct *mm_access(struct task_struct *task, unsigned int mode);
/* Remove the current tasks stale references to the old mm_struct */
extern void mm_release(struct task_struct *, struct mm_struct *);

extern int copy_thread(unsigned long, unsigned long, unsigned long,
			struct task_struct *);
extern void flush_thread(void);
extern void exit_thread(void);

extern void exit_files(struct task_struct *);
extern void __cleanup_sighand(struct sighand_struct *);

extern void exit_itimers(struct signal_struct *);
extern void flush_itimer_signals(void);

extern void do_group_exit(int);

extern int do_execve(struct filename *,
		     const char __user * const __user *,
		     const char __user * const __user *);
extern int do_execveat(int, struct filename *,
		       const char __user * const __user *,
		       const char __user * const __user *,
		       int);
extern long do_fork(unsigned long, unsigned long, unsigned long, int __user *, int __user *);
struct task_struct *fork_idle(int);
extern pid_t kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

extern void __set_task_comm(struct task_struct *tsk, const char *from, bool exec);
static inline void set_task_comm(struct task_struct *tsk, const char *from)
{
	__set_task_comm(tsk, from, false);
}
extern char *get_task_comm(char *to, struct task_struct *tsk);

#ifdef CONFIG_SMP
void scheduler_ipi(void);
extern unsigned long wait_task_inactive(struct task_struct *, long match_state);
#else
static inline void scheduler_ipi(void) { }
static inline unsigned long wait_task_inactive(struct task_struct *p,
					       long match_state)
{
	return 1;
}
#endif

#define next_task(p) \
	list_entry_rcu((p)->tasks.next, struct task_struct, tasks)

#define for_each_process(p) \
	for (p = &init_task ; (p = next_task(p)) != &init_task ; )

extern bool current_is_single_threaded(void);

/*
 * Careful: do_each_thread/while_each_thread is a double loop so
 *          'break' will not work as expected - use goto instead.
 */
#define do_each_thread(g, t) \
	for (g = t = &init_task ; (g = t = next_task(g)) != &init_task ; ) do

#define while_each_thread(g, t) \
	while ((t = next_thread(t)) != g)

#define __for_each_thread(signal, t)	\
	list_for_each_entry_rcu(t, &(signal)->thread_head, thread_node)

#define for_each_thread(p, t)		\
	__for_each_thread((p)->signal, t)

/* Careful: this is a double loop, 'break' won't work as expected. */
#define for_each_process_thread(p, t)	\
	for_each_process(p) for_each_thread(p, t)

static inline int get_nr_threads(struct task_struct *tsk)
{
	return tsk->signal->nr_threads;
}

static inline bool thread_group_leader(struct task_struct *p)
{
	return p->exit_signal >= 0;
}

/* Do to the insanities of de_thread it is possible for a process
 * to have the pid of the thread group leader without actually being
 * the thread group leader.  For iteration through the pids in proc
 * all we care about is that we have a task with the appropriate
 * pid, we don't actually care if we have the right task.
 */
static inline bool has_group_leader_pid(struct task_struct *p)
{
	return task_pid(p) == p->signal->leader_pid;
}

static inline
bool same_thread_group(struct task_struct *p1, struct task_struct *p2)
{
	return p1->signal == p2->signal;
}

static inline struct task_struct *next_thread(const struct task_struct *p)
{
	return list_entry_rcu(p->thread_group.next,
			      struct task_struct, thread_group);
}

static inline int thread_group_empty(struct task_struct *p)
{
	return list_empty(&p->thread_group);
}

#define delay_group_leader(p) \
		(thread_group_leader(p) && !thread_group_empty(p))

/*
 * Protects ->fs, ->files, ->mm, ->group_info, ->comm, keyring
 * subscriptions and synchronises with wait4().  Also used in procfs.  Also
 * pins the final release of task.io_context.  Also protects ->cpuset and
 * ->cgroup.subsys[]. And ->vfork_done.
 *
 * Nests both inside and outside of read_lock(&tasklist_lock).
 * It must not be nested with write_lock_irq(&tasklist_lock),
 * neither inside nor outside.
 */
static inline void task_lock(struct task_struct *p)
{
	spin_lock(&p->alloc_lock);
}

static inline void task_unlock(struct task_struct *p)
{
	spin_unlock(&p->alloc_lock);
}

extern struct sighand_struct *__lock_task_sighand(struct task_struct *tsk,
							unsigned long *flags);

static inline struct sighand_struct *lock_task_sighand(struct task_struct *tsk,
						       unsigned long *flags)
{
	struct sighand_struct *ret;

	ret = __lock_task_sighand(tsk, flags);
	(void)__cond_lock(&tsk->sighand->siglock, ret);
	return ret;
}

static inline void unlock_task_sighand(struct task_struct *tsk,
						unsigned long *flags)
{
	spin_unlock_irqrestore(&tsk->sighand->siglock, *flags);
}

#ifdef CONFIG_CGROUPS
static inline void threadgroup_change_begin(struct task_struct *tsk)
{
	down_read(&tsk->signal->group_rwsem);
}
static inline void threadgroup_change_end(struct task_struct *tsk)
{
	up_read(&tsk->signal->group_rwsem);
}

/**
 * threadgroup_lock - lock threadgroup
 * @tsk: member task of the threadgroup to lock
 *
 * Lock the threadgroup @tsk belongs to.  No new task is allowed to enter
 * and member tasks aren't allowed to exit (as indicated by PF_EXITING) or
 * change ->group_leader/pid.  This is useful for cases where the threadgroup
 * needs to stay stable across blockable operations.
 *
 * fork and exit paths explicitly call threadgroup_change_{begin|end}() for
 * synchronization.  While held, no new task will be added to threadgroup
 * and no existing live task will have its PF_EXITING set.
 *
 * de_thread() does threadgroup_change_{begin|end}() when a non-leader
 * sub-thread becomes a new leader.
 */
static inline void threadgroup_lock(struct task_struct *tsk)
{
	down_write(&tsk->signal->group_rwsem);
}

/**
 * threadgroup_unlock - unlock threadgroup
 * @tsk: member task of the threadgroup to unlock
 *
 * Reverse threadgroup_lock().
 */
static inline void threadgroup_unlock(struct task_struct *tsk)
{
	up_write(&tsk->signal->group_rwsem);
}
#else
static inline void threadgroup_change_begin(struct task_struct *tsk) {}
static inline void threadgroup_change_end(struct task_struct *tsk) {}
static inline void threadgroup_lock(struct task_struct *tsk) {}
static inline void threadgroup_unlock(struct task_struct *tsk) {}
#endif

#ifndef __HAVE_THREAD_FUNCTIONS

/* 获取指定线程的 struct task 结构体中的 thread_info 成员指针 */
#define task_thread_info(task)	((struct thread_info *)(task)->stack)

/* 获取指定线程的 struct task 结构体中的 stack 成员指针，这个成员指向了内核栈最低地址处
   即内核栈中 struct thread_info 结构的起始地址 */
#define task_stack_page(task)	((task)->stack)

/*********************************************************************************************************
** 函数名称: setup_thread_stack
** 功能描述: 根据指定的 task_struct 信息初始化指定的 task_struct 结构的内核栈信息
** 输	 入: p - 需要初始化的 task_struct 结构指针
**         : org - 指定的 task_struct 结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void setup_thread_stack(struct task_struct *p, struct task_struct *org)
{
	*task_thread_info(p) = *task_thread_info(org);
	task_thread_info(p)->task = p;
}

/*
 * Return the address of the last usable long on the stack.
 *
 * When the stack grows down, this is just above the thread
 * info struct. Going any lower will corrupt the threadinfo.
 *
 * When the stack grows up, this is the highest address.
 * Beyond that position, we corrupt data on the next page.
 */
/*********************************************************************************************************
** 函数名称: end_of_stack
** 功能描述: 获取指定任务的栈结构和它的 thread_info 的交界空洞地址，如果这个任务的栈地址越过这个
**         : 交界空洞地址则表示发生了栈溢出
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned long * - 交界空洞地址
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long *end_of_stack(struct task_struct *p)
{
#ifdef CONFIG_STACK_GROWSUP
	return (unsigned long *)((unsigned long)task_thread_info(p) + THREAD_SIZE) - 1;
#else
	return (unsigned long *)(task_thread_info(p) + 1);
#endif
}

#endif
/*********************************************************************************************************
** 函数名称: task_stack_end_corrupted
** 功能描述: 判断指定的任务是否发生了栈溢出
** 输	 入: p - 指定的任务指针
** 输	 出: 1 - 发生了栈溢出
**         : 0 - 没发生栈溢出
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define task_stack_end_corrupted(task) \
		(*(end_of_stack(task)) != STACK_END_MAGIC)

/*********************************************************************************************************
** 函数名称: object_is_on_stack
** 功能描述: 判断指定的对象地址是否在当前正在运行的任务栈空间中
** 输	 入: obj - 指定的对象地址
** 输	 出: 1 - 在当前正在运行的任务栈空间中
**         : 0 - 不在当前正在运行的任务栈空间中
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int object_is_on_stack(void *obj)
{
	void *stack = task_stack_page(current);

	return (obj >= stack) && (obj < (stack + THREAD_SIZE));
}

extern void thread_info_cache_init(void);

#ifdef CONFIG_DEBUG_STACK_USAGE
/*********************************************************************************************************
** 函数名称: stack_not_used
** 功能描述: 计算指定的任务的栈空间中从未被使用过的空间大小
** 输	 入: p - 指定的任务指针
** 输	 出: unsigned long - 从未被使用过的空间大小
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned long stack_not_used(struct task_struct *p)
{
	unsigned long *n = end_of_stack(p);

	do { 	/* Skip over canary */
		n++;
	} while (!*n);

	return (unsigned long)n - (unsigned long)end_of_stack(p);
}
#endif
extern void set_task_stack_end_magic(struct task_struct *tsk);

/* set thread flags in other task's structures
 * - see asm/thread_info.h for TIF_xxxx flags available
 */
/*********************************************************************************************************
** 函数名称: set_tsk_thread_flag
** 功能描述: 设置指定进程的指定 flag 标志位
** 输	 入: tsk - 指定的任务指针
**         : flag - 指定的 flag 标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	set_ti_thread_flag(task_thread_info(tsk), flag);
}

/*********************************************************************************************************
** 函数名称: clear_tsk_thread_flag
** 功能描述: 清除指定进程的指定 flag 标志位
** 输	 入: tsk - 指定的任务指针
**         : flag - 指定的 flag 标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	clear_ti_thread_flag(task_thread_info(tsk), flag);
}

/*********************************************************************************************************
** 函数名称: test_and_set_tsk_thread_flag
** 功能描述: 设置指定进程的指定 flag 标志位并返回原来的值
** 输	 入: tsk - 指定的任务指针
**         : flag - 指定的 flag 标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_and_set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_and_set_ti_thread_flag(task_thread_info(tsk), flag);
}

/*********************************************************************************************************
** 函数名称: test_and_clear_tsk_thread_flag
** 功能描述: 清除指定进程的指定 flag 标志位并返回原来的值
** 输	 入: tsk - 指定的任务指针
**         : flag - 指定的 flag 标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_and_clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_and_clear_ti_thread_flag(task_thread_info(tsk), flag);
}

/*********************************************************************************************************
** 函数名称: test_tsk_thread_flag
** 功能描述: 测试指定进程的指定的标志位是否被置位
** 输	 入: tsk - 指定的任务指针
**         : flag - 指定的 flag 标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_ti_thread_flag(task_thread_info(tsk), flag);
}

/*********************************************************************************************************
** 函数名称: set_tsk_need_resched
** 功能描述: 设置指定进程的 TIF_NEED_RESCHED 标志位
** 输	 入: tsk - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void set_tsk_need_resched(struct task_struct *tsk)
{
	set_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}

/*********************************************************************************************************
** 函数名称: clear_tsk_need_resched
** 功能描述: 清除指定进程的 TIF_NEED_RESCHED 标志位
** 输	 入: tsk - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void clear_tsk_need_resched(struct task_struct *tsk)
{
	clear_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}

/*********************************************************************************************************
** 函数名称: test_tsk_need_resched
** 功能描述: 测试指定进程的 TIF_NEED_RESCHED 标志位是否被置位
** 输	 入: tsk - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_tsk_need_resched(struct task_struct *tsk)
{
	return unlikely(test_tsk_thread_flag(tsk,TIF_NEED_RESCHED));
}

static inline int restart_syscall(void)
{
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	return -ERESTARTNOINTR;
}

/*********************************************************************************************************
** 函数名称: signal_pending
** 功能描述: 判断指定的线程是否有正在挂起的信号
** 输	 入: tsk - 指定的任务指针
** 输	 出: 1 - 有正在挂起的信号
**         : 0 - 没有正在挂起的信号
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int signal_pending(struct task_struct *p)
{
	return unlikely(test_tsk_thread_flag(p,TIF_SIGPENDING));
}

/*********************************************************************************************************
** 函数名称: __fatal_signal_pending
** 功能描述: 判断指定的线程挂起的信号中是否有 SIGKILL 成员
** 输	 入: tsk - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int __fatal_signal_pending(struct task_struct *p)
{
	return unlikely(sigismember(&p->pending.signal, SIGKILL));
}

/*********************************************************************************************************
** 函数名称: fatal_signal_pending
** 功能描述: 判断指定的线程是否正在挂起了 SIGKILL 信号
** 输	 入: tsk - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int fatal_signal_pending(struct task_struct *p)
{
	return signal_pending(p) && __fatal_signal_pending(p);
}

/*********************************************************************************************************
** 函数名称: signal_pending_state
** 功能描述: 判断指定状态的指定任务是否可以接收唤醒信号且接收到了一个唤醒信号
** 输	 入: state - 指定的任务当状态
**         : p - 指定的任务指针
** 输	 出: 1 - 可以且接收到了一个唤醒信号
**         : 0 - 不可以或没接收到了一个唤醒信号
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int signal_pending_state(long state, struct task_struct *p)
{
	if (!(state & (TASK_INTERRUPTIBLE | TASK_WAKEKILL)))
		return 0;
	if (!signal_pending(p))
		return 0;

	return (state & TASK_INTERRUPTIBLE) || __fatal_signal_pending(p);
}

/*
 * cond_resched() and cond_resched_lock(): latency reduction via
 * explicit rescheduling in places that are safe. The return
 * value indicates whether a reschedule was done in fact.
 * cond_resched_lock() will drop the spinlock before scheduling,
 * cond_resched_softirq() will enable bhs before scheduling.
 */
extern int _cond_resched(void);

/*********************************************************************************************************
** 函数名称: cond_resched
** 功能描述: 在安全的地方检查进程 TIF_NEED_RESCHED 标志决定是否需要执行进程调度操作，一般会在执行
**         : 比较耗时的操作之前调用，为了避免进程长时间得不到调度
** 输	 入: ret - 表示是否需要执行调度操作
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define cond_resched() ({			\
	___might_sleep(__FILE__, __LINE__, 0);	\
	_cond_resched();			\
})

extern int __cond_resched_lock(spinlock_t *lock);

#ifdef CONFIG_PREEMPT_COUNT
#define PREEMPT_LOCK_OFFSET	PREEMPT_OFFSET
#else
#define PREEMPT_LOCK_OFFSET	0
#endif

#define cond_resched_lock(lock) ({				\
	___might_sleep(__FILE__, __LINE__, PREEMPT_LOCK_OFFSET);\
	__cond_resched_lock(lock);				\
})

extern int __cond_resched_softirq(void);

#define cond_resched_softirq() ({					\
	___might_sleep(__FILE__, __LINE__, SOFTIRQ_DISABLE_OFFSET);	\
	__cond_resched_softirq();					\
})

static inline void cond_resched_rcu(void)
{
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP) || !defined(CONFIG_PREEMPT_RCU)
	rcu_read_unlock();
	cond_resched();
	rcu_read_lock();
#endif
}

/*
 * Does a critical section need to be broken due to another
 * task waiting?: (technically does not depend on CONFIG_PREEMPT,
 * but a general need for low latency)
 */
static inline int spin_needbreak(spinlock_t *lock)
{
#ifdef CONFIG_PREEMPT
	return spin_is_contended(lock);
#else
	return 0;
#endif
}

/*
 * Idle thread specific functions to determine the need_resched
 * polling state.
 */
#ifdef TIF_POLLING_NRFLAG
static inline int tsk_is_polling(struct task_struct *p)
{
	return test_tsk_thread_flag(p, TIF_POLLING_NRFLAG);
}

static inline void __current_set_polling(void)
{
	set_thread_flag(TIF_POLLING_NRFLAG);
}

static inline bool __must_check current_set_polling_and_test(void)
{
	__current_set_polling();

	/*
	 * Polling state must be visible before we test NEED_RESCHED,
	 * paired by resched_curr()
	 */
	smp_mb__after_atomic();

	return unlikely(tif_need_resched());
}

static inline void __current_clr_polling(void)
{
	clear_thread_flag(TIF_POLLING_NRFLAG);
}

static inline bool __must_check current_clr_polling_and_test(void)
{
	__current_clr_polling();

	/*
	 * Polling state must be visible before we test NEED_RESCHED,
	 * paired by resched_curr()
	 */
	smp_mb__after_atomic();

	return unlikely(tif_need_resched());
}

#else
static inline int tsk_is_polling(struct task_struct *p) { return 0; }
static inline void __current_set_polling(void) { }
static inline void __current_clr_polling(void) { }

static inline bool __must_check current_set_polling_and_test(void)
{
	return unlikely(tif_need_resched());
}
static inline bool __must_check current_clr_polling_and_test(void)
{
	return unlikely(tif_need_resched());
}
#endif

static inline void current_clr_polling(void)
{
	__current_clr_polling();

	/*
	 * Ensure we check TIF_NEED_RESCHED after we clear the polling bit.
	 * Once the bit is cleared, we'll get IPIs with every new
	 * TIF_NEED_RESCHED and the IPI handler, scheduler_ipi(), will also
	 * fold.
	 */
	smp_mb(); /* paired with resched_curr() */

	preempt_fold_need_resched();
}

/*********************************************************************************************************
** 函数名称: need_resched
** 功能描述: 判断当前正在运行的 cpu 上是否需要执行一次任务调度操作
** 输	 入: 
** 输	 出: 1 - 被置位
**         : 0 - 没被置位
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}

/*
 * Thread group CPU time accounting.
 */
void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times);
void thread_group_cputimer(struct task_struct *tsk, struct task_cputime *times);

static inline void thread_group_cputime_init(struct signal_struct *sig)
{
	raw_spin_lock_init(&sig->cputimer.lock);
}

/*
 * Reevaluate whether the task has signals pending delivery.
 * Wake the task if so.
 * This is required every time the blocked sigset_t changes.
 * callers must hold sighand->siglock.
 */
extern void recalc_sigpending_and_wake(struct task_struct *t);
extern void recalc_sigpending(void);

extern void signal_wake_up_state(struct task_struct *t, unsigned int state);

static inline void signal_wake_up(struct task_struct *t, bool resume)
{
	signal_wake_up_state(t, resume ? TASK_WAKEKILL : 0);
}
static inline void ptrace_signal_wake_up(struct task_struct *t, bool resume)
{
	signal_wake_up_state(t, resume ? __TASK_TRACED : 0);
}

/*
 * Wrappers for p->thread_info->cpu access. No-op on UP.
 */
#ifdef CONFIG_SMP
/*********************************************************************************************************
** 函数名称: task_cpu
** 功能描述: 获取指定任务在哪个 cpu 上运行
** 输	 入: p - 指定的 task_struct 结构指针
** 输	 出: unsigned int - 指定的任务所运行的 cpu 号
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned int task_cpu(const struct task_struct *p)
{
	return task_thread_info(p)->cpu;
}

/*********************************************************************************************************
** 函数名称: task_node
** 功能描述: 获取为指定的任务分配的 node id
** 输	 入: p - 指定的 task_struct 结构指针
** 输	 出: int - node id
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int task_node(const struct task_struct *p)
{
	return cpu_to_node(task_cpu(p));
}

extern void set_task_cpu(struct task_struct *p, unsigned int cpu);

#else
/*********************************************************************************************************
** 函数名称: task_cpu
** 功能描述: 获取指定任务在哪个 cpu 上运行
** 输	 入: p - 指定的 task_struct 结构指针
** 输	 出: unsigned int - 指定的任务所运行的 cpu 号
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline unsigned int task_cpu(const struct task_struct *p)
{
	return 0;
}

/*********************************************************************************************************
** 函数名称: set_task_cpu
** 功能描述: 把指定的任务从当前所在 cpu 运行队列上移除并设置新的目的 cpu 信息
** 输	 入: p - 指定的 task_struct 结构指针
**         : new_cpu - 指定的新的目的 cpu id
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void set_task_cpu(struct task_struct *p, unsigned int cpu)
{
}

#endif /* CONFIG_SMP */

extern long sched_setaffinity(pid_t pid, const struct cpumask *new_mask);
extern long sched_getaffinity(pid_t pid, struct cpumask *mask);

#ifdef CONFIG_CGROUP_SCHED
extern struct task_group root_task_group;
#endif /* CONFIG_CGROUP_SCHED */

extern int task_can_switch_user(struct user_struct *up,
					struct task_struct *tsk);

#ifdef CONFIG_TASK_XACCT
static inline void add_rchar(struct task_struct *tsk, ssize_t amt)
{
	tsk->ioac.rchar += amt;
}

static inline void add_wchar(struct task_struct *tsk, ssize_t amt)
{
	tsk->ioac.wchar += amt;
}

static inline void inc_syscr(struct task_struct *tsk)
{
	tsk->ioac.syscr++;
}

static inline void inc_syscw(struct task_struct *tsk)
{
	tsk->ioac.syscw++;
}
#else
static inline void add_rchar(struct task_struct *tsk, ssize_t amt)
{
}

static inline void add_wchar(struct task_struct *tsk, ssize_t amt)
{
}

static inline void inc_syscr(struct task_struct *tsk)
{
}

static inline void inc_syscw(struct task_struct *tsk)
{
}
#endif

#ifndef TASK_SIZE_OF
#define TASK_SIZE_OF(tsk)	TASK_SIZE
#endif

#ifdef CONFIG_MEMCG
extern void mm_update_next_owner(struct mm_struct *mm);
#else
static inline void mm_update_next_owner(struct mm_struct *mm)
{
}
#endif /* CONFIG_MEMCG */

static inline unsigned long task_rlimit(const struct task_struct *tsk,
		unsigned int limit)
{
	return ACCESS_ONCE(tsk->signal->rlim[limit].rlim_cur);
}

static inline unsigned long task_rlimit_max(const struct task_struct *tsk,
		unsigned int limit)
{
	return ACCESS_ONCE(tsk->signal->rlim[limit].rlim_max);
}

static inline unsigned long rlimit(unsigned int limit)
{
	return task_rlimit(current, limit);
}

static inline unsigned long rlimit_max(unsigned int limit)
{
	return task_rlimit_max(current, limit);
}

#endif

