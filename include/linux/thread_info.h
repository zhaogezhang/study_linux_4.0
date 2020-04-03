/* thread_info.h: common low-level thread information accessors
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds
 */

#ifndef _LINUX_THREAD_INFO_H
#define _LINUX_THREAD_INFO_H

#include <linux/types.h>
#include <linux/bug.h>

struct timespec;
struct compat_timespec;

/*
 * System call restart block.
 */
struct restart_block {
	long (*fn)(struct restart_block *);
	union {
		/* For futex_wait and futex_wait_requeue_pi */
		struct {
			u32 __user *uaddr;
			u32 val;
			u32 flags;
			u32 bitset;
			u64 time;
			u32 __user *uaddr2;
		} futex;
		/* For nanosleep */
		struct {
			clockid_t clockid;
			struct timespec __user *rmtp;
#ifdef CONFIG_COMPAT
			struct compat_timespec __user *compat_rmtp;
#endif
			u64 expires;
		} nanosleep;
		/* For poll */
		struct {
			struct pollfd __user *ufds;
			int nfds;
			int has_timeout;
			unsigned long tv_sec;
			unsigned long tv_nsec;
		} poll;
	};
};

extern long do_no_restart_syscall(struct restart_block *parm);

#include <linux/bitops.h>
#include <asm/thread_info.h>

#ifdef __KERNEL__

#ifdef CONFIG_DEBUG_STACK_USAGE
# define THREADINFO_GFP		(GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO)
#else
# define THREADINFO_GFP		(GFP_KERNEL | __GFP_NOTRACK)
#endif

/*
 * flag set/clear/test wrappers
 * - pass TIF_xxxx constants to these functions
 */

/*********************************************************************************************************
** 函数名称: set_ti_thread_flag
** 功能描述: 设置指定的的线程信息中的指定的 flag 标志位
** 输	 入: ti - 指定的线程信息
**         : flag - 指定的标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void set_ti_thread_flag(struct thread_info *ti, int flag)
{
	set_bit(flag, (unsigned long *)&ti->flags);
}

/*********************************************************************************************************
** 函数名称: clear_ti_thread_flag
** 功能描述: 清除指定的的线程信息中的指定的 flag 标志位
** 输	 入: ti - 指定的线程信息
**         : flag - 指定的标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline void clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	clear_bit(flag, (unsigned long *)&ti->flags);
}

/*********************************************************************************************************
** 函数名称: test_and_set_ti_thread_flag
** 功能描述: 测试并设置指定的的线程信息中的指定的 flag 标志位
** 输	 入: ti - 指定的线程信息
**         : flag - 指定的标志位
** 输	 出: return - 原来的标志值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_and_set_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_set_bit(flag, (unsigned long *)&ti->flags);
}

/*********************************************************************************************************
** 函数名称: test_and_clear_ti_thread_flag
** 功能描述: 测试并清除指定的的线程信息中的指定的 flag 标志位
** 输	 入: ti - 指定的线程信息
**         : flag - 指定的标志位
** 输	 出: return - 原来的标志值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_and_clear_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_and_clear_bit(flag, (unsigned long *)&ti->flags);
}

/*********************************************************************************************************
** 函数名称: set_ti_thread_flag
** 功能描述: 测试指定的的线程信息中的指定的 flag 标志位
** 输	 入: ti - 指定的线程信息
**         : flag - 指定的标志位
** 输	 出: 1 - 被置位
**         : 0 - 没被置位
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int test_ti_thread_flag(struct thread_info *ti, int flag)
{
	return test_bit(flag, (unsigned long *)&ti->flags);
}

/*********************************************************************************************************
** 函数名称: set_thread_flag
** 功能描述: 设置当前 cpu 当前正在运行的线程信息中的指定的 flag 标志位
** 输	 入: flag - 指定的标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define set_thread_flag(flag) \
	set_ti_thread_flag(current_thread_info(), flag)

/*********************************************************************************************************
** 函数名称: clear_thread_flag
** 功能描述: 清除当前 cpu 当前正在运行的线程信息中的指定的 flag 标志位
** 输	 入: flag - 指定的标志位
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define clear_thread_flag(flag) \
	clear_ti_thread_flag(current_thread_info(), flag)

/*********************************************************************************************************
** 函数名称: test_and_set_thread_flag
** 功能描述: 测试并设置当前 cpu 当前正在运行的线程信息中的指定的 flag 标志位
** 输	 入: flag - 指定的标志位
** 输	 出: return - 原来的标志值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define test_and_set_thread_flag(flag) \
	test_and_set_ti_thread_flag(current_thread_info(), flag)

/*********************************************************************************************************
** 函数名称: test_and_clear_thread_flag
** 功能描述: 测试并清除当前 cpu 当前正在运行的线程信息中的指定的 flag 标志位
** 输	 入: flag - 指定的标志位
** 输	 出: return - 原来的标志值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define test_and_clear_thread_flag(flag) \
	test_and_clear_ti_thread_flag(current_thread_info(), flag)

/*********************************************************************************************************
** 函数名称: test_thread_flag
** 功能描述: 测试当前 cpu 当前正在运行的线程信息中的指定的 flag 标志位是否被置位
** 输	 入: flag - 指定的标志位
** 输	 出:  1 - 被置位
**         : 0 - 没被置位
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define test_thread_flag(flag) \
	test_ti_thread_flag(current_thread_info(), flag)

/*********************************************************************************************************
** 函数名称: tif_need_resched
** 功能描述: 判断当前 cpu 当前正在运行的线程信息中的 TIF_NEED_RESCHED 标志位是否被置位
** 输	 入: 
** 输	 出: 1 - 被置位
**         : 0 - 没被置位
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define tif_need_resched() test_thread_flag(TIF_NEED_RESCHED)

#if defined TIF_RESTORE_SIGMASK && !defined HAVE_SET_RESTORE_SIGMASK
/*
 * An arch can define its own version of set_restore_sigmask() to get the
 * job done however works, with or without TIF_RESTORE_SIGMASK.
 */
#define HAVE_SET_RESTORE_SIGMASK	1

/**
 * set_restore_sigmask() - make sure saved_sigmask processing gets done
 *
 * This sets TIF_RESTORE_SIGMASK and ensures that the arch signal code
 * will run before returning to user mode, to process the flag.  For
 * all callers, TIF_SIGPENDING is already set or it's no harm to set
 * it.  TIF_RESTORE_SIGMASK need not be in the set of bits that the
 * arch code will notice on return to user mode, in case those bits
 * are scarce.  We set TIF_SIGPENDING here to ensure that the arch
 * signal code always gets run when TIF_RESTORE_SIGMASK is set.
 */
static inline void set_restore_sigmask(void)
{
	set_thread_flag(TIF_RESTORE_SIGMASK);
	WARN_ON(!test_thread_flag(TIF_SIGPENDING));
}
static inline void clear_restore_sigmask(void)
{
	clear_thread_flag(TIF_RESTORE_SIGMASK);
}
static inline bool test_restore_sigmask(void)
{
	return test_thread_flag(TIF_RESTORE_SIGMASK);
}
static inline bool test_and_clear_restore_sigmask(void)
{
	return test_and_clear_thread_flag(TIF_RESTORE_SIGMASK);
}
#endif	/* TIF_RESTORE_SIGMASK && !HAVE_SET_RESTORE_SIGMASK */

#ifndef HAVE_SET_RESTORE_SIGMASK
#error "no set_restore_sigmask() provided and default one won't work"
#endif

#endif	/* __KERNEL__ */

#endif /* _LINUX_THREAD_INFO_H */
