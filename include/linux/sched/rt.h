#ifndef _SCHED_RT_H
#define _SCHED_RT_H

#include <linux/sched/prio.h>

/*********************************************************************************************************
** 函数名称: rt_prio
** 功能描述: 判断指定的优先级是否是实时优先级
** 输	 入: prio - 指定的优先级
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int rt_prio(int prio)
{
	if (unlikely(prio < MAX_RT_PRIO))
		return 1;
	return 0;
}

/*********************************************************************************************************
** 函数名称: rt_task
** 功能描述: 判断指定的任务是否时实时任务
** 输	 入: prio - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int rt_task(struct task_struct *p)
{
	return rt_prio(p->prio);
}

#ifdef CONFIG_RT_MUTEXES
extern int rt_mutex_getprio(struct task_struct *p);
extern void rt_mutex_setprio(struct task_struct *p, int prio);
extern int rt_mutex_check_prio(struct task_struct *task, int newprio);
extern struct task_struct *rt_mutex_get_top_task(struct task_struct *task);
extern void rt_mutex_adjust_pi(struct task_struct *p);

/*********************************************************************************************************
** 函数名称: tsk_is_pi_blocked
** 功能描述: 判断是否存在正在等待指定任务持有的 rt_mutex 的任务
** 输	 入: tsk - 指定的任务指针
** 输	 出: 1 - 有
**         : 0 - 没有
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool tsk_is_pi_blocked(struct task_struct *tsk)
{
	return tsk->pi_blocked_on != NULL;
}
#else
static inline int rt_mutex_getprio(struct task_struct *p)
{
	return p->normal_prio;
}

static inline int rt_mutex_check_prio(struct task_struct *task, int newprio)
{
	return 0;
}

static inline struct task_struct *rt_mutex_get_top_task(struct task_struct *task)
{
	return NULL;
}
# define rt_mutex_adjust_pi(p)		do { } while (0)
/*********************************************************************************************************
** 函数名称: tsk_is_pi_blocked
** 功能描述: 判断是否存在正在等待指定任务持有的 rt_mutex 的任务
** 输	 入: tsk - 指定的任务指针
** 输	 出: 1 - 有
**         : 0 - 没有
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline bool tsk_is_pi_blocked(struct task_struct *tsk)
{
	return false;
}
#endif

extern void normalize_rt_tasks(void);


/*
 * default timeslice is 100 msecs (used only for SCHED_RR tasks).
 * Timeslices get refilled after they expire.
 */
#define RR_TIMESLICE		(100 * HZ / 1000)

#endif /* _SCHED_RT_H */
