#ifndef LINUX_PREEMPT_MASK_H
#define LINUX_PREEMPT_MASK_H

#include <linux/preempt.h>

/*
 * We put the hardirq and softirq counter into the preemption
 * counter. The bitmask has the following meaning:
 *
 * - bits 0-7 are the preemption count (max preemption depth: 256)
 * - bits 8-15 are the softirq count (max # of softirqs: 256)
 *
 * The hardirq count could in theory be the same as the number of
 * interrupts in the system, but we run all interrupt handlers with
 * interrupts disabled, so we cannot have nesting interrupts. Though
 * there are a few palaeontologic drivers which reenable interrupts in
 * the handler, so we need more than one bit here.
 *
 * PREEMPT_MASK:	0x000000ff
 * SOFTIRQ_MASK:	0x0000ff00
 * HARDIRQ_MASK:	0x000f0000
 *     NMI_MASK:	0x00100000
 * PREEMPT_ACTIVE:	0x00200000
 */
/* 表示不同的 flags 在 preempt_count 变量中占用的 bit 数 */
#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	4
#define NMI_BITS	    1 /* NMI - non mask interrupt */

/* 表示不同的 flags 在 preempt_count 变量中的位置的 bit 偏移量 */
#define PREEMPT_SHIFT	0                                /* 0  */
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)   /* 8  */
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)   /* 16 */
#define NMI_SHIFT	    (HARDIRQ_SHIFT + HARDIRQ_BITS)   /* 20 */

#define __IRQ_MASK(x)	((1UL << (x))-1)

#define PREEMPT_MASK	(__IRQ_MASK(PREEMPT_BITS) << PREEMPT_SHIFT)  /* 0x000000ff */
#define SOFTIRQ_MASK	(__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)  /* 0x0000ff00 */
#define HARDIRQ_MASK	(__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)  /* 0x000f0000 */
#define NMI_MASK	    (__IRQ_MASK(NMI_BITS)     << NMI_SHIFT)      /* 0x00100000 */

/* 表示不同的 flags 最低 bit 位表示的数值大小 */
#define PREEMPT_OFFSET	(1UL << PREEMPT_SHIFT)  /* 1       */
#define SOFTIRQ_OFFSET	(1UL << SOFTIRQ_SHIFT)  /* 256     */
#define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)  /* 65535   */
#define NMI_OFFSET	    (1UL << NMI_SHIFT)      /* ‭1048576‬ */

/* 表示我们在执行关闭软中断是需要对 preempt_count 执行的增量值 */
#define SOFTIRQ_DISABLE_OFFSET	(2 * SOFTIRQ_OFFSET)  /* 512 */

#define PREEMPT_ACTIVE_BITS	1
#define PREEMPT_ACTIVE_SHIFT	(NMI_SHIFT + NMI_BITS)   /* 21 */
#define PREEMPT_ACTIVE	(__IRQ_MASK(PREEMPT_ACTIVE_BITS) << PREEMPT_ACTIVE_SHIFT)  /* 0x00200000 */

/*********************************************************************************************************
** 函数名称: hardirq_count
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 hardirq 字段的值
** 输	 入: 
** 输	 出: int - hardirq 字段的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define hardirq_count()	(preempt_count() & HARDIRQ_MASK)

/*********************************************************************************************************
** 函数名称: softirq_count
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 softirq 字段的值
** 输	 入: 
** 输	 出: int - softirq 字段的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define softirq_count()	(preempt_count() & SOFTIRQ_MASK)

/*********************************************************************************************************
** 函数名称: irq_count
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 nmi | hardirq | softirq 字段的值
** 输	 入: 
** 输	 出: int - nmi | hardirq | softirq 字段的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define irq_count()	(preempt_count() & (HARDIRQ_MASK | SOFTIRQ_MASK \
				 | NMI_MASK))

/*
 * Are we doing bottom half or hardware interrupt processing?
 * Are we in a softirq context? Interrupt context?
 * in_softirq - Are we currently processing softirq or have bh disabled?
 * in_serving_softirq - Are we currently processing softirq?
 */
/*********************************************************************************************************
** 函数名称: in_irq
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 hardirq 字段的值，如果大于 0 则表示当前
**         : 正在硬件中断上下文中
** 输	 入: 
** 输	 出: int - hardirq 字段的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_irq()		(hardirq_count())

/*********************************************************************************************************
** 函数名称: in_softirq
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 softirq 字段的值，如果大于 0 则表示当前
**         : 正在软中断上下文中
** 输	 入: 
** 输	 出: int - softirq 字段的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_softirq()		(softirq_count())

/*********************************************************************************************************
** 函数名称: in_interrupt
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 nmi | hardirq | softirq 字段的值，如果
**         : 大于 0 则表示当前正在中断上下文中
** 输	 入: 
** 输	 出: int - nmi | hardirq | softirq 字段的值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_interrupt()		(irq_count())

/*********************************************************************************************************
** 函数名称: in_serving_softirq
** 功能描述: 获取当前正在运行的任务的 preempt_count 变量中 softirq 字段的最低 bit 为是否为 1
**         : 如果为 1 表示当前正在执行软中断处理函数
** 输	 入: 
** 输	 出: 1 - 为 1
**         : 0 - 不为 1
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_serving_softirq()	(softirq_count() & SOFTIRQ_OFFSET)

/*
 * Are we in NMI context?
 */
/*********************************************************************************************************
** 函数名称: in_nmi
** 功能描述: 判断当前是否在 NMI 中断上下文中
** 输	 入: 
** 输	 出: 1 - 在 NMI 中断上下文中
**         : 0 - 不在 NMI 中断上下文中
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_nmi()	(preempt_count() & NMI_MASK)

#if defined(CONFIG_PREEMPT_COUNT)
# define PREEMPT_CHECK_OFFSET 1
#else
# define PREEMPT_CHECK_OFFSET 0
#endif

/*
 * The preempt_count offset needed for things like:
 *
 *  spin_lock_bh()
 *
 * Which need to disable both preemption (CONFIG_PREEMPT_COUNT) and
 * softirqs, such that unlock sequences of:
 *
 *  spin_unlock();
 *  local_bh_enable();
 *
 * Work as expected.
 */
#define SOFTIRQ_LOCK_OFFSET (SOFTIRQ_DISABLE_OFFSET + PREEMPT_CHECK_OFFSET)

/*
 * Are we running in atomic context?  WARNING: this macro cannot
 * always detect atomic context; in particular, it cannot know about
 * held spinlocks in non-preemptible kernels.  Thus it should not be
 * used in the general case to determine whether sleeping is possible.
 * Do not use in_atomic() in driver code.
 */
/*********************************************************************************************************
** 函数名称: in_atomic
** 功能描述: 判断当前是否在原子操作上下文中
** 输	 入: 
** 输	 出: 1 - 在原子操作上下文中
**         : 0 - 不在原子操作上下文中
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_atomic()	((preempt_count() & ~PREEMPT_ACTIVE) != 0)

/*
 * Check whether we were atomic before we did preempt_disable():
 * (used by the scheduler, *after* releasing the kernel lock)
 */
/*********************************************************************************************************
** 函数名称: in_atomic_preempt_off
** 功能描述: 在执行关闭抢占操作之前判断当前是否在原子操作上下文中，由调度器代码使用
** 输	 入: 
** 输	 出: 1 - 在原子操作上下文中
**         : 0 - 不在原子操作上下文中
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define in_atomic_preempt_off() \
		((preempt_count() & ~PREEMPT_ACTIVE) != PREEMPT_CHECK_OFFSET)

#ifdef CONFIG_PREEMPT_COUNT
# define preemptible()	(preempt_count() == 0 && !irqs_disabled())
#else
# define preemptible()	0
#endif

#endif /* LINUX_PREEMPT_MASK_H */
