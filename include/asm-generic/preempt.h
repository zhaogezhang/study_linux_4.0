#ifndef __ASM_PREEMPT_H
#define __ASM_PREEMPT_H

#include <linux/thread_info.h>

#define PREEMPT_ENABLED	(0)

/*********************************************************************************************************
** 函数名称: preempt_count
** 功能描述: 获取当前正在运行的任务的 preempt_count 值
** 输	 入: 
** 输	 出: int - preempt_count 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline int preempt_count(void)
{
	return current_thread_info()->preempt_count;
}

/*********************************************************************************************************
** 函数名称: preempt_count_ptr
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量的指针
** 输	 入: 
** 输	 出: int - preempt_count 变量的指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline int *preempt_count_ptr(void)
{
	return &current_thread_info()->preempt_count;
}

/*********************************************************************************************************
** 函数名称: preempt_count_set
** 功能描述: 设置当前正在运行的任务的 preempt_count 为指定的值
** 输	 入: pc - 指定的新 preempt_count 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void preempt_count_set(int pc)
{
	*preempt_count_ptr() = pc;
}

/*
 * must be macros to avoid header recursion hell
 */
/*********************************************************************************************************
** 函数名称: init_task_preempt_count
** 功能描述: 把指定的任务初始化为不可抢占状态
** 输	 入: p - 指定的任务指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define init_task_preempt_count(p) do { \
	task_thread_info(p)->preempt_count = PREEMPT_DISABLED; \
} while (0)

/*********************************************************************************************************
** 函数名称: init_idle_preempt_count
** 功能描述: 把指定的任务初始化为可以抢占状态
** 输	 入: p - 指定的任务指针
**         : cpu - 未使用
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define init_idle_preempt_count(p, cpu) do { \
	task_thread_info(p)->preempt_count = PREEMPT_ENABLED; \
} while (0)

static __always_inline void set_preempt_need_resched(void)
{
}

static __always_inline void clear_preempt_need_resched(void)
{
}

static __always_inline bool test_preempt_need_resched(void)
{
	return false;
}

/*
 * The various preempt_count add/sub methods
 */
/*********************************************************************************************************
** 函数名称: __preempt_count_add
** 功能描述: 把当前正在运行的任务的 preempt_count 变量加上指定的值
** 输	 入: val - 指定的增量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void __preempt_count_add(int val)
{
	*preempt_count_ptr() += val;
}

/*********************************************************************************************************
** 函数名称: __preempt_count_sub
** 功能描述: 把当前正在运行的任务的 preempt_count 变量减去指定的值
** 输	 入: val - 指定的减量值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline void __preempt_count_sub(int val)
{
	*preempt_count_ptr() -= val;
}

/*********************************************************************************************************
** 函数名称: __preempt_count_dec_and_test
** 功能描述: 对当前正在运行的任务的 preempt_count 变量递减并判断当前任务是否可以且需要执行任务调度
** 输	 入: 
** 输	 出: 1 - 可以且需要执行任务调度
**         : 0 - 不可以或不需要执行任务调度
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline bool __preempt_count_dec_and_test(void)
{
	/*
	 * Because of load-store architectures cannot do per-cpu atomic
	 * operations; we cannot use PREEMPT_NEED_RESCHED because it might get
	 * lost.
	 */
	return !--*preempt_count_ptr() && tif_need_resched();
}

/*
 * Returns true when we need to resched and can (barring IRQ state).
 */
/*********************************************************************************************************
** 函数名称: should_resched
** 功能描述: 判断当前正在运行的任务的是否可以且需要执行任务调度
** 输	 入: 
** 输	 出: 1 - 可以且需要执行任务调度
**         : 0 - 不可以或不需要执行任务调度
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __always_inline bool should_resched(void)
{
	return unlikely(!preempt_count() && tif_need_resched());
}

#ifdef CONFIG_PREEMPT
extern asmlinkage void preempt_schedule(void);
#define __preempt_schedule() preempt_schedule()

#ifdef CONFIG_CONTEXT_TRACKING
extern asmlinkage void preempt_schedule_context(void);
#define __preempt_schedule_context() preempt_schedule_context()
#endif
#endif /* CONFIG_PREEMPT */

#endif /* __ASM_PREEMPT_H */
