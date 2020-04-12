#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H

/*
 * include/linux/preempt.h - macros for accessing and manipulating
 * preempt_count (used for kernel preemption, interrupt count, etc.)
 */

#include <linux/linkage.h>
#include <linux/list.h>

/*
 * We use the MSB mostly because its available; see <linux/preempt_mask.h> for
 * the other bits -- can't include that header due to inclusion hell.
 */
#define PREEMPT_NEED_RESCHED	0x80000000

#include <asm/preempt.h>

#if defined(CONFIG_DEBUG_PREEMPT) || defined(CONFIG_PREEMPT_TRACER)
/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 把当前正在运行的任务的 preempt_count 变量加上指定的值
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
extern void preempt_count_add(int val);

/*********************************************************************************************************
** 函数名称: __preempt_count_sub
** 功能描述: 把当前正在运行的任务的 preempt_count 变量减去指定的值
** 输	 入: val - 指定的减量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
extern void preempt_count_sub(int val);

/*********************************************************************************************************
** 函数名称: preempt_count_dec_and_test
** 功能描述: 对当前正在运行的任务的 preempt_count 变量递减并判断当前任务是否可以且需要执行任务调度
** 输	 入: 
** 输	 出: 1 - 可以且需要执行任务调度
**         : 0 - 不可以或不需要执行任务调度
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_count_dec_and_test() ({ preempt_count_sub(1); should_resched(); })
#else
/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 把当前正在运行的任务的 preempt_count 变量加上指定的值
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_count_add(val)	__preempt_count_add(val)

/*********************************************************************************************************
** 函数名称: __preempt_count_sub
** 功能描述: 把当前正在运行的任务的 preempt_count 变量减去指定的值
** 输	 入: val - 指定的减量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_count_sub(val)	__preempt_count_sub(val)

/*********************************************************************************************************
** 函数名称: preempt_count_dec_and_test
** 功能描述: 对当前正在运行的任务的 preempt_count 变量递减并判断当前任务是否可以且需要执行任务调度
** 输	 入: 
** 输	 出: 1 - 可以且需要执行任务调度
**         : 0 - 不可以或不需要执行任务调度
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_count_dec_and_test() __preempt_count_dec_and_test()
#endif

/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 对当前正在运行的任务的 preempt_count 递增
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __preempt_count_inc() __preempt_count_add(1)

/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 对当前正在运行的任务的 preempt_count 递减
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define __preempt_count_dec() __preempt_count_sub(1)

/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 对当前正在运行的任务的 preempt_count 递增
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_count_inc() preempt_count_add(1)

/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 对当前正在运行的任务的 preempt_count 递减
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_count_dec() preempt_count_sub(1)

#ifdef CONFIG_PREEMPT_COUNT

/*********************************************************************************************************
** 函数名称: preempt_disable
** 功能描述: 递增当前 cpu 的 preempt_count 计数值
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_disable() \
do { \
	preempt_count_inc(); \
	barrier(); \
} while (0)

/*********************************************************************************************************
** 函数名称: sched_preempt_enable_no_resched
** 功能描述: 递减当前 cpu 的 preempt_count 计数值
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define sched_preempt_enable_no_resched() \
do { \
	barrier(); \
	preempt_count_dec(); \
} while (0)

/*********************************************************************************************************
** 函数名称: sched_preempt_enable_no_resched
** 功能描述: 递减当前 cpu 的 preempt_count 计数值
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_enable_no_resched() sched_preempt_enable_no_resched()

#ifdef CONFIG_PREEMPT
/*********************************************************************************************************
** 函数名称: preempt_enable
** 功能描述: 递减当前 cpu 的 preempt_count 计数值并尝试执行一次抢占式任务调度
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_enable() \
do { \
	barrier(); \
	if (unlikely(preempt_count_dec_and_test())) \
		__preempt_schedule(); \
} while (0)

/*********************************************************************************************************
** 函数名称: preempt_check_resched
** 功能描述: 尝试执行一次抢占式任务调度
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_check_resched() \
do { \
	if (should_resched()) \
		__preempt_schedule(); \
} while (0)

#else
/*********************************************************************************************************
** 函数名称: preempt_enable
** 功能描述: 递减当前 cpu 的 preempt_count 计数值
** 注     释: 在不开启抢占功能时，不尝试执行抢占式任务调度
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_enable() \
do { \
	barrier(); \
	preempt_count_dec(); \
} while (0)

/*********************************************************************************************************
** 函数名称: preempt_check_resched
** 功能描述: 在不开启抢占功能时，不尝试执行抢占式任务调度
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_check_resched() do { } while (0)
#endif

/*********************************************************************************************************
** 函数名称: preempt_disable_notrace
** 功能描述: 对当前正在运行的任务的 preempt_count 递增
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_disable_notrace() \
do { \
	__preempt_count_inc(); \
	barrier(); \
} while (0)

/*********************************************************************************************************
** 函数名称: preempt_enable_no_resched_notrace
** 功能描述: 对当前正在运行的任务的 preempt_count 递减
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_enable_no_resched_notrace() \
do { \
	barrier(); \
	__preempt_count_dec(); \
} while (0)

#ifdef CONFIG_PREEMPT

#ifndef CONFIG_CONTEXT_TRACKING
#define __preempt_schedule_context() __preempt_schedule()
#endif

/*********************************************************************************************************
** 函数名称: preempt_enable_notrace
** 功能描述: 对当前正在运行的任务的 preempt_count 递减并尝试执行任务调度
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_enable_notrace() \
do { \
	barrier(); \
	if (unlikely(__preempt_count_dec_and_test())) \
		__preempt_schedule_context(); \
} while (0)
#else
/*********************************************************************************************************
** 函数名称: preempt_enable_notrace
** 功能描述: 对当前正在运行的任务的 preempt_count 递减
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define preempt_enable_notrace() \
do { \
	barrier(); \
	__preempt_count_dec(); \
} while (0)
#endif

#else /* !CONFIG_PREEMPT_COUNT */

/*
 * Even if we don't have any preemption, we need preempt disable/enable
 * to be barriers, so that we don't have things like get_user/put_user
 * that can cause faults and scheduling migrate into our preempt-protected
 * region.
 */
#define preempt_disable()			barrier()
#define sched_preempt_enable_no_resched()	barrier()
#define preempt_enable_no_resched()		barrier()
#define preempt_enable()			barrier()
#define preempt_check_resched()			do { } while (0)

#define preempt_disable_notrace()		barrier()
#define preempt_enable_no_resched_notrace()	barrier()
#define preempt_enable_notrace()		barrier()

#endif /* CONFIG_PREEMPT_COUNT */

#ifdef MODULE
/*
 * Modules have no business playing preemption tricks.
 */
#undef sched_preempt_enable_no_resched
#undef preempt_enable_no_resched
#undef preempt_enable_no_resched_notrace
#undef preempt_check_resched
#endif

#define preempt_set_need_resched() \
do { \
	set_preempt_need_resched(); \
} while (0)
#define preempt_fold_need_resched() \
do { \
	if (tif_need_resched()) \
		set_preempt_need_resched(); \
} while (0)

#ifdef CONFIG_PREEMPT_NOTIFIERS

struct preempt_notifier;

/**
 * preempt_ops - notifiers called when a task is preempted and rescheduled
 * @sched_in: we're about to be rescheduled:
 *    notifier: struct preempt_notifier for the task being scheduled
 *    cpu:  cpu we're scheduled on
 * @sched_out: we've just been preempted
 *    notifier: struct preempt_notifier for the task being preempted
 *    next: the task that's kicking us out
 *
 * Please note that sched_in and out are called under different
 * contexts.  sched_out is called with rq lock held and irq disabled
 * while sched_in is called without rq lock and irq enabled.  This
 * difference is intentional and depended upon by its users.
 */
struct preempt_ops {
	void (*sched_in)(struct preempt_notifier *notifier, int cpu);
	void (*sched_out)(struct preempt_notifier *notifier,
			  struct task_struct *next);
};

/**
 * preempt_notifier - key for installing preemption notifiers
 * @link: internal use
 * @ops: defines the notifier functions to be called
 *
 * Usually used in conjunction with container_of().
 */
struct preempt_notifier {
	struct hlist_node link;
	struct preempt_ops *ops;
};

void preempt_notifier_register(struct preempt_notifier *notifier);
void preempt_notifier_unregister(struct preempt_notifier *notifier);

static inline void preempt_notifier_init(struct preempt_notifier *notifier,
				     struct preempt_ops *ops)
{
	INIT_HLIST_NODE(&notifier->link);
	notifier->ops = ops;
}

#endif

#endif /* __LINUX_PREEMPT_H */
