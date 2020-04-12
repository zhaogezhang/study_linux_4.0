#ifndef _SCHED_DEADLINE_H
#define _SCHED_DEADLINE_H

/*
 * SCHED_DEADLINE tasks has negative priorities, reflecting
 * the fact that any of them has higher prio than RT and
 * NORMAL/BATCH tasks.
 */

#define MAX_DL_PRIO		0

/*********************************************************************************************************
** 函数名称: dl_prio
** 功能描述: 判断指定的优先级是否是 DEADLINE 优先级
** 输	 入: prio - 指定的优先级
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int dl_prio(int prio)
{
	if (unlikely(prio < MAX_DL_PRIO))
		return 1;
	return 0;
}

/*********************************************************************************************************
** 函数名称: dl_task
** 功能描述: 判断指定的任务是否是 DEADLINE 任务
** 输	 入: p - 指定的任务指针
** 输	 出: 1 - 是
**         : 0 - 不是
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int dl_task(struct task_struct *p)
{
	return dl_prio(p->prio);
}

#endif /* _SCHED_DEADLINE_H */
