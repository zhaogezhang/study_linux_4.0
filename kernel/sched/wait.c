/*
 * Generic waiting primitives.
 *
 * (C) 2004 Nadia Yvette Chambers, Oracle
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/hash.h>
#include <linux/kthread.h>

/*********************************************************************************************************
** 函数名称: __init_waitqueue_head
** 功能描述: 根据函数指定的参数初始化指定的等待队列头结构
** 输	 入: q - 指定的等待队列头指针
**         : name - 指定的死锁检测名
**         : key - 指定的死锁检测键值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __init_waitqueue_head(wait_queue_head_t *q, const char *name, struct lock_class_key *key)
{
	spin_lock_init(&q->lock);
	lockdep_set_class_and_name(&q->lock, key, name);
	INIT_LIST_HEAD(&q->task_list);
}

EXPORT_SYMBOL(__init_waitqueue_head);

/*********************************************************************************************************
** 函数名称: add_wait_queue
** 功能描述: 将指定的等待者成员以“非独占”方式添加到指定的等待队列的“头”部位置
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待成员指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void add_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	wait->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(add_wait_queue);

/*********************************************************************************************************
** 函数名称: add_wait_queue
** 功能描述: 将指定的等待者成员以“独占”方式添加到指定的等待队列的“尾”部位置
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待成员指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void add_wait_queue_exclusive(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	__add_wait_queue_tail(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(add_wait_queue_exclusive);

/*********************************************************************************************************
** 函数名称: remove_wait_queue
** 功能描述: 将指定的等待者成员从指定的等待队列中移除
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待成员指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void remove_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__remove_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(remove_wait_queue);

/*
 * The core wakeup function. Non-exclusive wakeups (nr_exclusive == 0) just
 * wake everything up. If it's an exclusive wakeup (nr_exclusive == small +ve
 * number) then we wake all the non-exclusive tasks and one exclusive task.
 *
 * There are circumstances in which we can try to wake a task which has already
 * started to run but is not in state TASK_RUNNING. try_to_wake_up() returns
 * zero in this (rare) case, and we handle it by continuing to scan the queue.
 */
/*********************************************************************************************************
** 函数名称: __wake_up_common
** 功能描述: 从指定的等待队列上按照指定的参数唤醒指定个数的等待者
** 输	 入: q - 指定的等待队列头指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : nr_exclusive - 表示需要按照独占方式唤醒的等待者个数
**         : wake_flags - 指定的 wakeup flags，例如 WF_FORK
**         : key - 指定的键值指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void __wake_up_common(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, int wake_flags, void *key)
{
	wait_queue_t *curr, *next;

    /* 遍历指定的等待队列的等待者链表，尝试唤醒每一个等待者成员 */
	list_for_each_entry_safe(curr, next, &q->task_list, task_list) {
		unsigned flags = curr->flags;

		if (curr->func(curr, mode, wake_flags, key) &&
				(flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}

/**
 * __wake_up - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: is directly passed to the wakeup function
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
/*********************************************************************************************************
** 函数名称: __wake_up
** 功能描述: 从指定的等待队列上按照指定的参数唤醒指定个数的等待者
** 输	 入: q - 指定的等待队列头指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : nr_exclusive - 表示需要按照独占方式唤醒的等待者个数
**         : key - 指定的键值指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __wake_up(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, 0, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(__wake_up);

/*
 * Same as __wake_up but called with the spinlock in wait_queue_head_t held.
 */
/*********************************************************************************************************
** 函数名称: __wake_up_locked
** 功能描述: 从指定的等待队列上按照指定的参数唤醒指定个数的等待者
** 注     释: 这个函数在已经持有指定等待队列自旋锁的情况下调用
** 输	 入: q - 指定的等待队列头指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : nr_exclusive - 表示需要按照独占方式唤醒的等待者个数
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __wake_up_locked(wait_queue_head_t *q, unsigned int mode, int nr)
{
	__wake_up_common(q, mode, nr, 0, NULL);
}
EXPORT_SYMBOL_GPL(__wake_up_locked);

/*********************************************************************************************************
** 函数名称: __wake_up_locked_key
** 功能描述: 从指定的等待队列上按照指定的参数唤醒一个等待者
** 输	 入: q - 指定的等待队列头指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : key - 指定的键值指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __wake_up_locked_key(wait_queue_head_t *q, unsigned int mode, void *key)
{
	__wake_up_common(q, mode, 1, 0, key);
}
EXPORT_SYMBOL_GPL(__wake_up_locked_key);

/**
 * __wake_up_sync_key - wake up threads blocked on a waitqueue.
 * @q: the waitqueue
 * @mode: which threads
 * @nr_exclusive: how many wake-one or wake-many threads to wake up
 * @key: opaque value to be passed to wakeup targets
 *
 * The sync wakeup differs that the waker knows that it will schedule
 * away soon, so while the target thread will be woken up, it will not
 * be migrated to another CPU - ie. the two threads are 'synchronized'
 * with each other. This can prevent needless bouncing between CPUs.
 *
 * On UP it can prevent extra preemption.
 *
 * It may be assumed that this function implies a write memory barrier before
 * changing the task state if and only if any tasks are woken up.
 */
/*********************************************************************************************************
** 函数名称: __wake_up_sync_key
** 功能描述: 从指定的等待队列上以同步（WF_SYNC）执行方式按照指定的参数唤醒指定个数的等待者
** 输	 入: q - 指定的等待队列头指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : nr_exclusive - 表示需要按照独占方式唤醒的等待者个数
**         : key - 指定的键值指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __wake_up_sync_key(wait_queue_head_t *q, unsigned int mode,
			int nr_exclusive, void *key)
{
	unsigned long flags;
	int wake_flags = 1; /* XXX WF_SYNC */

	if (unlikely(!q))
		return;

	if (unlikely(nr_exclusive != 1))
		wake_flags = 0;

	spin_lock_irqsave(&q->lock, flags);
	__wake_up_common(q, mode, nr_exclusive, wake_flags, key);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL_GPL(__wake_up_sync_key);

/*
 * __wake_up_sync - see __wake_up_sync_key()
 */
/*********************************************************************************************************
** 函数名称: __wake_up_sync
** 功能描述: 从指定的等待队列上以同步（WF_SYNC）执行方式按照指定的参数唤醒指定个数的等待者
** 输	 入: q - 指定的等待队列头指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : nr_exclusive - 表示需要按照独占方式唤醒的等待者个数
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __wake_up_sync(wait_queue_head_t *q, unsigned int mode, int nr_exclusive)
{
	__wake_up_sync_key(q, mode, nr_exclusive, NULL);
}
EXPORT_SYMBOL_GPL(__wake_up_sync);	/* For internal use only */

/*
 * Note: we use "set_current_state()" _after_ the wait-queue add,
 * because we need a memory barrier there on SMP, so that any
 * wake-function that tests for the wait-queue being active
 * will be guaranteed to see waitqueue addition _or_ subsequent
 * tests in this thread will see the wakeup having taken place.
 *
 * The spin_unlock() itself is semi-permeable and only protects
 * one way (it only protects stuff inside the critical region and
 * stops them from bleeding out - it would still allow subsequent
 * loads to move into the critical region).
 */
/*********************************************************************************************************
** 函数名称: prepare_to_wait
** 功能描述: 把为当前正在运行的任务封装的等待者结构以指定的等待状态按照“非独占”方式添加到指定的
**         : 等待队列的头部位置
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待者指针
**         : state - 指定的等待者状态
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void
prepare_to_wait(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	wait->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue(q, wait);
	set_current_state(state);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(prepare_to_wait);

/*********************************************************************************************************
** 函数名称: prepare_to_wait_exclusive
** 功能描述: 把为当前正在运行的任务封装的等待者结构以指定的等待状态按照“独占”方式添加到指定的
**         : 等待队列的尾部位置
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待者指针
**         : state - 指定的等待者状态
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void
prepare_to_wait_exclusive(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	wait->flags |= WQ_FLAG_EXCLUSIVE;
	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue_tail(q, wait);
	set_current_state(state);
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(prepare_to_wait_exclusive);

/*********************************************************************************************************
** 函数名称: prepare_to_wait_event
** 功能描述: 根据函数参数尝试为当前正在运行的任务封装一个等待事件的等待者结构并添加到指定的等待队列中
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待者指针
**         : state - 指定的等待者状态
** 输	 出: 0 - 添加成功
**         : -ERESTARTSYS - 需要重新添加
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
long prepare_to_wait_event(wait_queue_head_t *q, wait_queue_t *wait, int state)
{
	unsigned long flags;

	if (signal_pending_state(state, current))
		return -ERESTARTSYS;

	wait->private = current;
	wait->func = autoremove_wake_function;

	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list)) {
		if (wait->flags & WQ_FLAG_EXCLUSIVE)
			__add_wait_queue_tail(q, wait);
		else
			__add_wait_queue(q, wait);
	}
	set_current_state(state);
	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}
EXPORT_SYMBOL(prepare_to_wait_event);

/**
 * finish_wait - clean up after waiting in a queue
 * @q: waitqueue waited on
 * @wait: wait descriptor
 *
 * Sets current thread back to running state and removes
 * the wait descriptor from the given waitqueue if still
 * queued.
 */
/*********************************************************************************************************
** 函数名称: finish_wait
** 功能描述: 将当前正在运行的任务恢复到 TASK_RUNNING 状态并将其等待者结构从指定的等待队列中移除
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待者指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void finish_wait(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);
	/*
	 * We can check for list emptiness outside the lock
	 * IFF:
	 *  - we use the "careful" check that verifies both
	 *    the next and prev pointers, so that there cannot
	 *    be any half-pending updates in progress on other
	 *    CPU's that we haven't seen yet (and that might
	 *    still change the stack area.
	 * and
	 *  - all other users take the lock (ie we can only
	 *    have _one_ other CPU that looks at or modifies
	 *    the list).
	 */
	if (!list_empty_careful(&wait->task_list)) {
		spin_lock_irqsave(&q->lock, flags);
		list_del_init(&wait->task_list);
		spin_unlock_irqrestore(&q->lock, flags);
	}
}
EXPORT_SYMBOL(finish_wait);

/**
 * abort_exclusive_wait - abort exclusive waiting in a queue
 * @q: waitqueue waited on
 * @wait: wait descriptor
 * @mode: runstate of the waiter to be woken
 * @key: key to identify a wait bit queue or %NULL
 *
 * Sets current thread back to running state and removes
 * the wait descriptor from the given waitqueue if still
 * queued.
 *
 * Wakes up the next waiter if the caller is concurrently
 * woken up through the queue.
 *
 * This prevents waiter starvation where an exclusive waiter
 * aborts and is woken up concurrently and no one wakes up
 * the next waiter.
 */
/*********************************************************************************************************
** 函数名称: abort_exclusive_wait
** 功能描述: 将当前正在执行的、处于独占式等待者的任务从其所属等待队列中移除并恢复运行状态
** 输	 入: q - 指定的等待队列头指针
**         : wait - 指定的等待者指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : key - 指定的键值指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void abort_exclusive_wait(wait_queue_head_t *q, wait_queue_t *wait,
			unsigned int mode, void *key)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);
	spin_lock_irqsave(&q->lock, flags);

	/* 如果指定的等待者没被其它任务唤醒，则直接将其从指定的等待队列中移除，如果指定的
	   等待者已经被其它任务唤醒，则尝试唤醒指定等待队列中的下一个等待者成员，这样做的
	   目的是为了防止独占式等待者在被唤醒的同时执行了 abort 操作导致没有人去唤醒其后
	   且相邻的等待者，造成其后且相邻的等待者的“饥饿”现象 */
	if (!list_empty(&wait->task_list))
		list_del_init(&wait->task_list);
	else if (waitqueue_active(q))
		__wake_up_locked_key(q, mode, key);
	
	spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(abort_exclusive_wait);

/*********************************************************************************************************
** 函数名称: autoremove_wake_function
** 功能描述: 根据函数参数将指定的等待者从其所属等待队列上唤醒并移除
** 输	 入: wait - 指定的等待者指针
**         : mode - 指定的任务匹配状态，例如 TASK_NORMAL
**         : sync - 表示是否以同步方式唤醒等待者
**         : key - 指定的键值指针
** 输	 出: true - 唤醒成功
**         : false - 指定的任务已经是运行状态或者指定的任务状态不匹配
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int autoremove_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	int ret = default_wake_function(wait, mode, sync, key);

	if (ret)
		list_del_init(&wait->task_list);
	return ret;
}
EXPORT_SYMBOL(autoremove_wake_function);

/*********************************************************************************************************
** 函数名称: is_kthread_should_stop
** 功能描述: 判断当前正在运行的线程是否为内核线程且需要停止运行并返回
** 输	 入: 
** 输	 出: 1 - 是内核线程且需要停止运行并返回
**         : 0 - 不是内核线程或者不需要停止运行并返回
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool is_kthread_should_stop(void)
{
	return (current->flags & PF_KTHREAD) && kthread_should_stop();
}

/*
 * DEFINE_WAIT_FUNC(wait, woken_wake_func);
 *
 * add_wait_queue(&wq, &wait);
 * for (;;) {
 *     if (condition)
 *         break;
 *
 *     p->state = mode;				condition = true;
 *     smp_mb(); // A				smp_wmb(); // C
 *     if (!wait->flags & WQ_FLAG_WOKEN)	wait->flags |= WQ_FLAG_WOKEN;
 *         schedule()				try_to_wake_up();
 *     p->state = TASK_RUNNING;		    ~~~~~~~~~~~~~~~~~~
 *     wait->flags &= ~WQ_FLAG_WOKEN;		condition = true;
 *     smp_mb() // B				smp_wmb(); // C
 *						wait->flags |= WQ_FLAG_WOKEN;
 * }
 * remove_wait_queue(&wq, &wait);
 *
 */
/*********************************************************************************************************
** 函数名称: wait_woken
** 功能描述: 将当前正在运行的任务切换出去并睡眠指定的时间来等待被其它的任务唤醒
** 输	 入: wait - 指定的等待者指针
**         : mode - 指定的等待者模式
**         : timeout - 指定的等待超时时间，单位是 tick 周期
** 输	 出: 0  - 等待超时
**         : >0 - 提前被唤醒的时间长度，单位是 tick 周期
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
long wait_woken(wait_queue_t *wait, unsigned mode, long timeout)
{
	set_current_state(mode); /* A */
	/*
	 * The above implies an smp_mb(), which matches with the smp_wmb() from
	 * woken_wake_function() such that if we observe WQ_FLAG_WOKEN we must
	 * also observe all state before the wakeup.
	 */
	if (!(wait->flags & WQ_FLAG_WOKEN) && !is_kthread_should_stop())
		timeout = schedule_timeout(timeout);
	__set_current_state(TASK_RUNNING);

	/*
	 * The below implies an smp_mb(), it too pairs with the smp_wmb() from
	 * woken_wake_function() such that we must either observe the wait
	 * condition being true _OR_ WQ_FLAG_WOKEN such that we will not miss
	 * an event.
	 */
	set_mb(wait->flags, wait->flags & ~WQ_FLAG_WOKEN); /* B */

	return timeout;
}
EXPORT_SYMBOL(wait_woken);

/*********************************************************************************************************
** 函数名称: woken_wake_function
** 功能描述: 根据函数指定的参数尝试唤醒指定的等待者
** 输	 入: wait - 指定的等待者指针
**         : mode - 指定的等待者模式
**         : sync - 是否为同步换新模式
**         : key - 指定的键值指针
** 输	 出: true - 唤醒成功
**         : false - 指定的任务已经是运行状态或者指定的任务状态不匹配
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int woken_wake_function(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	/*
	 * Although this function is called under waitqueue lock, LOCK
	 * doesn't imply write barrier and the users expects write
	 * barrier semantics on wakeup functions.  The following
	 * smp_wmb() is equivalent to smp_wmb() in try_to_wake_up()
	 * and is paired with set_mb() in wait_woken().
	 */
	smp_wmb(); /* C */
	wait->flags |= WQ_FLAG_WOKEN;

	return default_wake_function(wait, mode, sync, key);
}
EXPORT_SYMBOL(woken_wake_function);

/*********************************************************************************************************
** 函数名称: wake_bit_function
** 功能描述: 根据函数参数尝试唤醒指定的位变量等待者
** 输	 入: wait - 指定的位变量等待者指针
**         : mode - 指定的位变量等待者模式
**         : sync - 是否为同步换新模式
**         : key - 指定的键值指针，表示本次想要唤醒的位变量等待者的键值信息
** 输	 出: true - 唤醒成功
**         : false - 指定的任务已经是运行状态或者指定的任务状态不匹配
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int wake_bit_function(wait_queue_t *wait, unsigned mode, int sync, void *arg)
{
	struct wait_bit_key *key = arg;
	struct wait_bit_queue *wait_bit
		= container_of(wait, struct wait_bit_queue, wait);

	if (wait_bit->key.flags != key->flags ||
			wait_bit->key.bit_nr != key->bit_nr ||
			test_bit(key->bit_nr, key->flags))
		return 0;
	else
		return autoremove_wake_function(wait, mode, sync, key);
}
EXPORT_SYMBOL(wake_bit_function);

/*
 * To allow interruptible waiting and asynchronous (i.e. nonblocking)
 * waiting, the actions of __wait_on_bit() and __wait_on_bit_lock() are
 * permitted return codes. Nonzero return codes halt waiting and return.
 */
/*********************************************************************************************************
** 函数名称: __wait_on_bit
** 功能描述: 将指定的位变量等待者添加到指定的等待队列上并通过指定的睡眠函数进入睡眠状态直到
**         : 我们关心的位变量被清除
** 输	 入: wq - 指定的等待队列头指针
**         : q - 指定的位变量等待者结构指针
**         : action - 指定的等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
** 输	 出: 0  - 等待超时
**         : >0 - 被其他任务成功唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched
__wait_on_bit(wait_queue_head_t *wq, struct wait_bit_queue *q,
	      wait_bit_action_f *action, unsigned mode)
{
	int ret = 0;

    /* 通过指定的睡眠函数进入睡眠状态直到我们关心的位变量标志被清除 */
	do {
		prepare_to_wait(wq, &q->wait, mode);
		
		if (test_bit(q->key.bit_nr, q->key.flags))
			ret = (*action)(&q->key);
		
	} while (test_bit(q->key.bit_nr, q->key.flags) && !ret);
		
	finish_wait(wq, &q->wait);
	return ret;
}
EXPORT_SYMBOL(__wait_on_bit);

/*********************************************************************************************************
** 函数名称: out_of_line_wait_on_bit
** 功能描述: 通过指定的睡眠函数进入睡眠状态直到指定的位图掩码值中指定的 bit 位被清除
** 输	 入: word - 指定的位图掩码值
**         : bit - 在指定的位图掩码值中关心的位变量
**         : action - 指定的等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
** 输	 出: 0  - 等待超时
**         : >0 - 被其他任务成功唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched out_of_line_wait_on_bit(void *word, int bit,
				    wait_bit_action_f *action, unsigned mode)
{
	wait_queue_head_t *wq = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wait, word, bit);

	return __wait_on_bit(wq, &wait, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_bit);

/*********************************************************************************************************
** 函数名称: out_of_line_wait_on_bit
** 功能描述: 通过指定的睡眠函数进入睡眠状态指定时间尝试等待指定的位图掩码值中指定的 bit 位被清除
** 输	 入: word - 指定的位图掩码值
**         : bit - 在指定的位图掩码值中关心的位变量
**         : action - 指定的等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
**         : timeout - 指定的超时时间，单位为 tick 周期
** 输	 出: 0  - 等待超时
**         : >0 - 被其他任务成功唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched out_of_line_wait_on_bit_timeout(
	void *word, int bit, wait_bit_action_f *action,
	unsigned mode, unsigned long timeout)
{
	wait_queue_head_t *wq = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wait, word, bit);

	wait.key.timeout = jiffies + timeout;
	return __wait_on_bit(wq, &wait, action, mode);
}
EXPORT_SYMBOL_GPL(out_of_line_wait_on_bit_timeout);

/*********************************************************************************************************
** 函数名称: __wait_on_bit_lock
** 功能描述: 把指定的位变量等待者添加到指定的等待队列中并通过指定的睡眠函数进入睡眠状态等待
**         : 直到我们关心的位变量被清除
** 输	 入: wq - 指定的等待队列头指针
**         : q - 指定的位变量等待者结构指针
**         : action - 指定的位变量等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
** 输	 出: 0  - 我们关心的位变量被清除
**         : >0 - 被其他任务唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched
__wait_on_bit_lock(wait_queue_head_t *wq, struct wait_bit_queue *q,
			wait_bit_action_f *action, unsigned mode)
{
	do {
		int ret;

		prepare_to_wait_exclusive(wq, &q->wait, mode);
		if (!test_bit(q->key.bit_nr, q->key.flags))
			continue;
		
		ret = action(&q->key);
		if (!ret)
			continue;
		
		abort_exclusive_wait(wq, &q->wait, mode, &q->key);
		return ret;
		
	} while (test_and_set_bit(q->key.bit_nr, q->key.flags));
	
	finish_wait(wq, &q->wait);
	
	return 0;
}
EXPORT_SYMBOL(__wait_on_bit_lock);

/*********************************************************************************************************
** 函数名称: out_of_line_wait_on_bit_lock
** 功能描述: 根据指定的位图掩码值和位变量构建一个位变量等待者并将其添加到所属等待队列中，然后通过指定的
**         : 睡眠函数进入睡眠状态等待，直到我们关心的位变量被清除
** 输	 入: word - 指定的位图掩码值
**         : bit - 在指定的位图掩码值中关心的位变量
**         : action - 指定的位变量等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
** 输	 出: 0  - 我们关心的位变量被清除
**         : >0 - 被其他任务唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __sched out_of_line_wait_on_bit_lock(void *word, int bit,
					 wait_bit_action_f *action, unsigned mode)
{
	wait_queue_head_t *wq = bit_waitqueue(word, bit);
	DEFINE_WAIT_BIT(wait, word, bit);

	return __wait_on_bit_lock(wq, &wait, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_bit_lock);

/*********************************************************************************************************
** 函数名称: __wake_up_bit
** 功能描述: 根据函数参数尝试从指定的等待队列中唤醒一个等待指定位图掩码中指定位变量的等待者
** 注     释: 在我们关心的位变量被清除后唤醒指定的等待者
** 输	 入: q - 指定的等待队列头指针
**         : word - 指定的位图掩码值
**         : bit - 在指定的位图掩码值中关心的位变量
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __wake_up_bit(wait_queue_head_t *wq, void *word, int bit)
{
	struct wait_bit_key key = __WAIT_BIT_KEY_INITIALIZER(word, bit);
	if (waitqueue_active(wq))
		__wake_up(wq, TASK_NORMAL, 1, &key);
}
EXPORT_SYMBOL(__wake_up_bit);

/**
 * wake_up_bit - wake up a waiter on a bit
 * @word: the word being waited on, a kernel virtual address
 * @bit: the bit of the word being waited on
 *
 * There is a standard hashed waitqueue table for generic use. This
 * is the part of the hashtable's accessor API that wakes up waiters
 * on a bit. For instance, if one were to have waiters on a bitflag,
 * one would call wake_up_bit() after clearing the bit.
 *
 * In order for this to function properly, as it uses waitqueue_active()
 * internally, some kind of memory barrier must be done prior to calling
 * this. Typically, this will be smp_mb__after_atomic(), but in some
 * cases where bitflags are manipulated non-atomically under a lock, one
 * may need to use a less regular barrier, such fs/inode.c's smp_mb(),
 * because spin_unlock() does not guarantee a memory barrier.
 */
/*********************************************************************************************************
** 函数名称: wake_up_bit
** 功能描述: 根据函数参数尝试唤醒一个等待指定位图掩码中指定位变量的等待者
** 注     释: 在我们关心的位变量被清除后唤醒指定的等待者
** 输	 入:  word - 指定的位图掩码值（内核虚拟地址）
**         : bit - 在指定的位图掩码值中关心的位变量
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void wake_up_bit(void *word, int bit)
{
	__wake_up_bit(bit_waitqueue(word, bit), word, bit);
}
EXPORT_SYMBOL(wake_up_bit);

/*********************************************************************************************************
** 函数名称: bit_waitqueue
** 功能描述: 根据函数参数计算与其对应的等待队列头指针
** 输	 入: word - 指定的位图掩码值（内核虚拟地址）
**         : bit - 我们关心的位变量位置
** 输	 出: 对应的等待队列头指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
wait_queue_head_t *bit_waitqueue(void *word, int bit)
{
	const int shift = BITS_PER_LONG == 32 ? 5 : 6;
	const struct zone *zone = page_zone(virt_to_page(word));
	unsigned long val = (unsigned long)word << shift | bit;

	return &zone->wait_table[hash_long(val, zone->wait_table_bits)];
}
EXPORT_SYMBOL(bit_waitqueue);

/*
 * Manipulate the atomic_t address to produce a better bit waitqueue table hash
 * index (we're keying off bit -1, but that would produce a horrible hash
 * value).
 */
/*********************************************************************************************************
** 函数名称: atomic_t_waitqueue
** 功能描述: 获取指定的原子位变量对应的等待队列头指针
** 输	 入: p - 指定的原子位变量指针
** 输	 出: 对应的等待队列头指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline wait_queue_head_t *atomic_t_waitqueue(atomic_t *p)
{
	if (BITS_PER_LONG == 64) {
		unsigned long q = (unsigned long)p;
		return bit_waitqueue((void *)(q & ~1), q & 1);
	}
	return bit_waitqueue(p, 0);
}

/*********************************************************************************************************
** 函数名称: wake_atomic_t_function
** 功能描述: 根据函数参数尝试唤醒指定的原子位变量等待者
** 输	 入: wait - 指定的位变量等待者指针
**         : mode - 指定的位变量等待者模式
**         : sync - 是否为同步换新模式
**         : arg - 指定的键值指针，表示本次想要唤醒的位变量等待者的键值信息
** 输	 出: true - 唤醒成功
**         : false - 指定的任务已经是运行状态或者指定的任务状态不匹配
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int wake_atomic_t_function(wait_queue_t *wait, unsigned mode, int sync,
				  void *arg)
{
	struct wait_bit_key *key = arg;
	struct wait_bit_queue *wait_bit
		= container_of(wait, struct wait_bit_queue, wait);
	atomic_t *val = key->flags;

	if (wait_bit->key.flags != key->flags ||
	    wait_bit->key.bit_nr != key->bit_nr ||
	    atomic_read(val) != 0)
		return 0;
	
	return autoremove_wake_function(wait, mode, sync, key);
}

/*
 * To allow interruptible waiting and asynchronous (i.e. nonblocking) waiting,
 * the actions of __wait_on_atomic_t() are permitted return codes.  Nonzero
 * return codes halt waiting and return.
 */
/*********************************************************************************************************
** 函数名称: __wait_on_atomic_t
** 功能描述: 把指定的原子位变量等待者添加到指定的等待队列中并通过指定的睡眠函数进入睡眠状态等待
**         : “直到”指定的原子位变量被清零位置
** 输	 入: wq - 指定的等待队列头指针
**         : q - 指定的原子位变量等待者结构指针
**         : action - 指定的位变量等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
** 输	 出: 0  - 等待超时
**         : >0 - 被其他任务唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static __sched
int __wait_on_atomic_t(wait_queue_head_t *wq, struct wait_bit_queue *q,
		       int (*action)(atomic_t *), unsigned mode)
{
	atomic_t *val;
	int ret = 0;

	do {
		prepare_to_wait(wq, &q->wait, mode);
		val = q->key.flags;
		if (atomic_read(val) == 0)
			break;
		ret = (*action)(val);
	} while (!ret && atomic_read(val) != 0);
	
	finish_wait(wq, &q->wait);
	return ret;
}

/*********************************************************************************************************
** 函数名称: DEFINE_WAIT_ATOMIC_T
** 功能描述: 根据函数参数声明并初始化一个原子位变量等待者结构
** 输	 入: name - 指定的原子位变量等待者名称
**         : p - 指定的原子位变量地址
** 输	 出: name - 初始化完的原子位变量等待者结构
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define DEFINE_WAIT_ATOMIC_T(name, p)					\
	struct wait_bit_queue name = {					\
		.key = __WAIT_ATOMIC_T_KEY_INITIALIZER(p),		\
		.wait	= {						\
			.private	= current,			\
			.func		= wake_atomic_t_function,	\
			.task_list	=				\
				LIST_HEAD_INIT((name).wait.task_list),	\
		},							\
	}

/*********************************************************************************************************
** 函数名称: out_of_line_wait_on_atomic_t
** 功能描述: 根据指定的原子变量指针构建一个原子位变量等待者，然后将其添加到所属的等待队列中并
**         : 通过指定的睡眠函数进入睡眠状态等待，“直到”指定的原子位变量被清零位置
** 输	 入: p - 指定的原子位变量指针
**         : action - 指定的位变量等待者睡眠函数指针
**         : mode - 指定的等待者状态模式
** 输	 出: 0  - 等待超时
**         : >0 - 被其他任务唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
__sched int out_of_line_wait_on_atomic_t(atomic_t *p, int (*action)(atomic_t *),
					 unsigned mode)
{
	wait_queue_head_t *wq = atomic_t_waitqueue(p);
	DEFINE_WAIT_ATOMIC_T(wait, p);

	return __wait_on_atomic_t(wq, &wait, action, mode);
}
EXPORT_SYMBOL(out_of_line_wait_on_atomic_t);

/**
 * wake_up_atomic_t - Wake up a waiter on a atomic_t
 * @p: The atomic_t being waited on, a kernel virtual address
 *
 * Wake up anyone waiting for the atomic_t to go to zero.
 *
 * Abuse the bit-waker function and its waitqueue hash table set (the atomic_t
 * check is done by the waiter's wake function, not the by the waker itself).
 */
/*********************************************************************************************************
** 函数名称: wake_up_atomic_t
** 功能描述: 从指定原子位变量所属等待队列中唤醒一个原子位变量的等待者
** 注     释: 在我们关心的原子位变量被清除后唤醒指定的等待者
** 输	 入: p - 指定的原子位变量指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void wake_up_atomic_t(atomic_t *p)
{
	__wake_up_bit(atomic_t_waitqueue(p), p, WAIT_ATOMIC_T_BIT_NR);
}
EXPORT_SYMBOL(wake_up_atomic_t);

/*********************************************************************************************************
** 函数名称: bit_wait
** 功能描述: 使当前正在运行的任务被切换出去进入睡眠状态，用来等待指定的位变量被清除，用来等待
**         : 指定的位变量被清除
** 输	 入: word - 未使用
** 输	 出: 1 - 无法进入睡眠
**         : 0 - 成功进入睡眠且被唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
__sched int bit_wait(struct wait_bit_key *word)
{
	if (signal_pending_state(current->state, current))
		return 1;
	
	schedule();
	return 0;
}
EXPORT_SYMBOL(bit_wait);

/*********************************************************************************************************
** 函数名称: bit_wait_io
** 功能描述: 使当前正在运行的任务被切换出去进入睡眠状态，用来等待指定的位变量被清除，用来等待
**         : 指定的位变量被清除
** 注     释: 这个函数用在 IO 睡眠等待的场景下
** 输	 入: word - 未使用
** 输	 出: 1 - 无法进入睡眠
**         : 0 - 成功进入睡眠且被唤醒
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
__sched int bit_wait_io(struct wait_bit_key *word)
{
	if (signal_pending_state(current->state, current))
		return 1;
	
	io_schedule();
	return 0;
}
EXPORT_SYMBOL(bit_wait_io);

/*********************************************************************************************************
** 函数名称: bit_wait_timeout
** 功能描述: 使当前正在运行的任务被切换出去进入睡眠状态指定的时间长度，用来等待指定的位变量被清除
** 输	 入: word - 指定的位变量等待者指针
** 输	 出: 1 - 无法进入睡眠
**         : 0 - 成功进入睡眠且被唤醒
**         : -EAGAIN - 时间参数错误，需要调整后重新执行一次
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
__sched int bit_wait_timeout(struct wait_bit_key *word)
{
	unsigned long now = ACCESS_ONCE(jiffies);
	if (signal_pending_state(current->state, current))
		return 1;
	
	if (time_after_eq(now, word->timeout))
		return -EAGAIN;
	
	schedule_timeout(word->timeout - now);
	return 0;
}
EXPORT_SYMBOL_GPL(bit_wait_timeout);

/*********************************************************************************************************
** 函数名称: bit_wait_io_timeout
** 功能描述: 使当前正在运行的任务被切换出去进入睡眠状态指定的时间长度，用来等待指定的位变量被清除
** 注     释: 这个函数用在 IO 睡眠等待的场景下
** 输	 入: word - 指定的位变量等待者指针
** 输	 出: 1 - 无法进入睡眠
**         : 0 - 成功进入睡眠且被唤醒
**         : -EAGAIN - 时间参数错误，需要调整后重新执行一次
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
__sched int bit_wait_io_timeout(struct wait_bit_key *word)
{
	unsigned long now = ACCESS_ONCE(jiffies);
	if (signal_pending_state(current->state, current))
		return 1;
	
	if (time_after_eq(now, word->timeout))
		return -EAGAIN;
	
	io_schedule_timeout(word->timeout - now);
	return 0;
}
EXPORT_SYMBOL_GPL(bit_wait_io_timeout);
