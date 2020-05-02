/*
 * Generic wait-for-completion handler;
 *
 * It differs from semaphores in that their default case is the opposite,
 * wait_for_completion default blocks whereas semaphore default non-block. The
 * interface also makes it easy to 'complete' multiple waiting threads,
 * something which isn't entirely natural for semaphores.
 *
 * But more importantly, the primitive documents the usage. Semaphores would
 * typically be used for exclusion which gives rise to priority inversion.
 * Waiting for completion is a typically sync point, but not an exclusion point.
 */

#include <linux/sched.h>
#include <linux/completion.h>

/**
 * complete: - signals a single thread waiting on this completion
 * @x:  holds the state of this particular completion
 *
 * This will wake up a single thread waiting on this completion. Threads will be
 * awakened in the same order in which they were queued.
 *
 * See also complete_all(), wait_for_completion() and related routines.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
/*********************************************************************************************************
** 函数名称: complete
** 功能描述: 唤醒指定条件变量上的第一个任务
** 输	 入: x - 指定的条件变量指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void complete(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done++;
	__wake_up_locked(&x->wait, TASK_NORMAL, 1);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete);

/**
 * complete_all: - signals all threads waiting on this completion
 * @x:  holds the state of this particular completion
 *
 * This will wake up all threads waiting on this particular completion event.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
/*********************************************************************************************************
** 函数名称: complete_all
** 功能描述: 唤醒指定条件变量上的所有任务
** 输	 入: x - 指定的条件变量指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void complete_all(struct completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->wait.lock, flags);
	x->done += UINT_MAX/2;
	__wake_up_locked(&x->wait, TASK_NORMAL, 0);
	spin_unlock_irqrestore(&x->wait.lock, flags);
}
EXPORT_SYMBOL(complete_all);

/*********************************************************************************************************
** 函数名称: do_wait_for_common
** 功能描述: 使当前正在运行的任务进入指定的睡眠状态并通过指定的睡眠函数睡眠指定的时间来等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
**         : action - 指定的睡眠函数指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
**         : state - 为当前任务指定的睡眠状态
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline long __sched
do_wait_for_common(struct completion *x,
		   long (*action)(long), long timeout, int state)
{
    /* 如果指定的条件变量已经被触发过，则更新 x->done 字段值并返回 */
	if (!x->done) {
		DECLARE_WAITQUEUE(wait, current);

        /* 把当前任务添加到指定的条件变量的等待队列中 */
		__add_wait_queue_tail_exclusive(&x->wait, &wait);
	
		do {
			if (signal_pending_state(state, current)) {
				timeout = -ERESTARTSYS;
				break;
			}

			/* 设置当前任务为指定的睡眠状态，这样就会被调度切换出去运行其他任务，详情见 __schedule 函数 */
			__set_current_state(state);

            /* 释放锁并主动执行调度操作进入指定的睡眠状态 */			
			spin_unlock_irq(&x->wait.lock);
			timeout = action(timeout);
			spin_lock_irq(&x->wait.lock);
			
		} while (!x->done && timeout);

		/* 把当前任务从指定条件变量的等待队列中移除 */
		__remove_wait_queue(&x->wait, &wait);
		
		if (!x->done)
			return timeout;
	}

	/* 表示当前任务已经成功获取指定的条件变量，则同步更新有效条件变量计数值 */
	x->done--;
	
	return timeout ?: 1;
}

/*********************************************************************************************************
** 函数名称: __wait_for_common
** 功能描述: 使当前正在运行的任务进入指定的睡眠状态并通过指定的睡眠函数睡眠指定的时间来等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
**         : action - 指定的睡眠函数指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
**         : state - 为当前任务指定的睡眠状态
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline long __sched
__wait_for_common(struct completion *x,
		  long (*action)(long), long timeout, int state)
{
	might_sleep();

	spin_lock_irq(&x->wait.lock);
	timeout = do_wait_for_common(x, action, timeout, state);
	spin_unlock_irq(&x->wait.lock);
	return timeout;
}

/*********************************************************************************************************
** 函数名称: wait_for_common
** 功能描述: 使当前正在运行的任务进入指定的睡眠状态并通过 schedule_timeout 函数睡眠指定的时间来
**         : 等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
**         : state - 为当前任务指定的睡眠状态
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static long __sched
wait_for_common(struct completion *x, long timeout, int state)
{
	return __wait_for_common(x, schedule_timeout, timeout, state);
}

/*********************************************************************************************************
** 函数名称: wait_for_common_io
** 功能描述: 使当前正在运行的任务进入指定的睡眠状态并通过 io_schedule_timeout 函数睡眠指定的时间来
**         : 等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
**         : state - 为当前任务指定的睡眠状态
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static long __sched
wait_for_common_io(struct completion *x, long timeout, int state)
{
	return __wait_for_common(x, io_schedule_timeout, timeout, state);
}

/**
 * wait_for_completion: - waits for completion of a task
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It is NOT
 * interruptible and there is no timeout.
 *
 * See also similar routines (i.e. wait_for_completion_timeout()) with timeout
 * and interrupt capability. Also see complete().
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion
** 功能描述: 使当前正在运行的任务进入 TASK_UNINTERRUPTIBLE 状态直到被指定的条件变量唤醒
** 注     释: 这个函数是用来等待普通条件变量的
** 输	 入: x - 指定的条件变量指针
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __sched wait_for_completion(struct completion *x)
{
	wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion);

/**
 * wait_for_completion_timeout: - waits for completion of a task (w/timeout)
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. The timeout is in jiffies. It is not
 * interruptible.
 *
 * Return: 0 if timed out, and positive (at least 1, or number of jiffies left
 * till timeout) if completed.
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_timeout
** 功能描述: 使当前正在运行的任务进入 TASK_UNINTERRUPTIBLE 状态睡眠指定的时间来等待指定的条件变量
** 注     释: 这个函数是用来等待普通条件变量的
** 输	 入: x - 指定的条件变量指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
unsigned long __sched
wait_for_completion_timeout(struct completion *x, unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_timeout);

/**
 * wait_for_completion_io: - waits for completion of a task
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It is NOT
 * interruptible and there is no timeout. The caller is accounted as waiting
 * for IO (which traditionally means blkio only).
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_io
** 功能描述: 使当前正在运行的任务进入 TASK_UNINTERRUPTIBLE 状态直到被指定的条件变量唤醒
** 注     释: 这个函数是用来等待 IO 条件变量的
** 输	 入: x - 指定的条件变量指针
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __sched wait_for_completion_io(struct completion *x)
{
	wait_for_common_io(x, MAX_SCHEDULE_TIMEOUT, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_io);

/**
 * wait_for_completion_io_timeout: - waits for completion of a task (w/timeout)
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. The timeout is in jiffies. It is not
 * interruptible. The caller is accounted as waiting for IO (which traditionally
 * means blkio only).
 *
 * Return: 0 if timed out, and positive (at least 1, or number of jiffies left
 * till timeout) if completed.
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_io_timeout
** 功能描述: 使当前正在运行的任务进入 TASK_UNINTERRUPTIBLE 状态睡眠指定的时间来等待指定的条件变量
** 注     释: 这个函数是用来等待 IO 条件变量的
** 输	 入: x - 指定的条件变量指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
unsigned long __sched
wait_for_completion_io_timeout(struct completion *x, unsigned long timeout)
{
	return wait_for_common_io(x, timeout, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_io_timeout);

/**
 * wait_for_completion_interruptible: - waits for completion of a task (w/intr)
 * @x:  holds the state of this particular completion
 *
 * This waits for completion of a specific task to be signaled. It is
 * interruptible.
 *
 * Return: -ERESTARTSYS if interrupted, 0 if completed.
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_interruptible
** 功能描述: 使当前正在运行的任务进入 TASK_INTERRUPTIBLE 状态直到被指定的条件变量唤醒
** 输	 入: x - 指定的条件变量指针
** 输	 出: 0  - 被指定的条件变量唤醒
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched wait_for_completion_interruptible(struct completion *x)
{
	long t = wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_INTERRUPTIBLE);
	if (t == -ERESTARTSYS)
		return t;
	return 0;
}
EXPORT_SYMBOL(wait_for_completion_interruptible);

/**
 * wait_for_completion_interruptible_timeout: - waits for completion (w/(to,intr))
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be signaled or for a
 * specified timeout to expire. It is interruptible. The timeout is in jiffies.
 *
 * Return: -ERESTARTSYS if interrupted, 0 if timed out, positive (at least 1,
 * or number of jiffies left till timeout) if completed.
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_interruptible
** 功能描述: 使当前正在运行的任务进入 TASK_INTERRUPTIBLE 状态睡眠指定的时间来等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
long __sched
wait_for_completion_interruptible_timeout(struct completion *x,
					  unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_INTERRUPTIBLE);
}
EXPORT_SYMBOL(wait_for_completion_interruptible_timeout);

/**
 * wait_for_completion_killable: - waits for completion of a task (killable)
 * @x:  holds the state of this particular completion
 *
 * This waits to be signaled for completion of a specific task. It can be
 * interrupted by a kill signal.
 *
 * Return: -ERESTARTSYS if interrupted, 0 if completed.
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_killable
** 功能描述: 使当前正在运行的任务进入 TASK_KILLABLE 状态直到被指定的条件变量唤醒
** 输	 入: x - 指定的条件变量指针
** 输	 出: 0  - 被指定的条件变量唤醒
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched wait_for_completion_killable(struct completion *x)
{
	long t = wait_for_common(x, MAX_SCHEDULE_TIMEOUT, TASK_KILLABLE);
	if (t == -ERESTARTSYS)
		return t;
	return 0;
}
EXPORT_SYMBOL(wait_for_completion_killable);

/**
 * wait_for_completion_killable_timeout: - waits for completion of a task (w/(to,killable))
 * @x:  holds the state of this particular completion
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific task to be
 * signaled or for a specified timeout to expire. It can be
 * interrupted by a kill signal. The timeout is in jiffies.
 *
 * Return: -ERESTARTSYS if interrupted, 0 if timed out, positive (at least 1,
 * or number of jiffies left till timeout) if completed.
 */
/*********************************************************************************************************
** 函数名称: wait_for_completion_killable_timeout
** 功能描述: 使当前正在运行的任务进入 TASK_KILLABLE 状态睡眠指定的时间来等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
**         : timeout - 指定的睡眠超时时间，单位为一个 tick 周期
** 输	 出: 0  - 等待超时
**         : >0 - 提前唤醒的时间长度，单位为一个 tick 周期
**         : -ERESTARTSYS - 被信号量唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
long __sched
wait_for_completion_killable_timeout(struct completion *x,
				     unsigned long timeout)
{
	return wait_for_common(x, timeout, TASK_KILLABLE);
}
EXPORT_SYMBOL(wait_for_completion_killable_timeout);

/**
 *	try_wait_for_completion - try to decrement a completion without blocking
 *	@x:	completion structure
 *
 *	Return: 0 if a decrement cannot be done without blocking
 *		 1 if a decrement succeeded.
 *
 *	If a completion is being used as a counting completion,
 *	attempt to decrement the counter without blocking. This
 *	enables us to avoid waiting if the resource the completion
 *	is protecting is not available.
 */
/*********************************************************************************************************
** 函数名称: try_wait_for_completion
** 功能描述: 以非阻塞模式尝试等待指定的条件变量
** 输	 入: x - 指定的条件变量指针
** 输	 出: 1 - 等待成功
**         : 0 - 等待失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
bool try_wait_for_completion(struct completion *x)
{
	unsigned long flags;
	int ret = 1;

	/*
	 * Since x->done will need to be locked only
	 * in the non-blocking case, we check x->done
	 * first without taking the lock so we can
	 * return early in the blocking case.
	 */
	if (!READ_ONCE(x->done))
		return 0;

	spin_lock_irqsave(&x->wait.lock, flags);
	if (!x->done)
		ret = 0;
	else
		x->done--;
	spin_unlock_irqrestore(&x->wait.lock, flags);
	return ret;
}
EXPORT_SYMBOL(try_wait_for_completion);

/**
 *	completion_done - Test to see if a completion has any waiters
 *	@x:	completion structure
 *
 *	Return: 0 if there are waiters (wait_for_completion() in progress)
 *		 1 if there are no waiters.
 *
 */
/*********************************************************************************************************
** 函数名称: completion_done
** 功能描述: 判断指定的条件变量上是否还有可以被获取的有效条件变量
** 输	 入: x - 指定的条件变量指针
** 输	 出: 1 - 有可以被获取的有效条件变量
**         : 0 - 没有可以被获取的有效条件变量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
bool completion_done(struct completion *x)
{
	if (!READ_ONCE(x->done))
		return false;

	/*
	 * If ->done, we need to wait for complete() to release ->wait.lock
	 * otherwise we can end up freeing the completion before complete()
	 * is done referencing it.
	 *
	 * The RMB pairs with complete()'s RELEASE of ->wait.lock and orders
	 * the loads of ->done and ->wait.lock such that we cannot observe
	 * the lock before complete() acquires it while observing the ->done
	 * after it's acquired the lock.
	 */
	smp_rmb();
	spin_unlock_wait(&x->wait.lock);
	return true;
}
EXPORT_SYMBOL(completion_done);
