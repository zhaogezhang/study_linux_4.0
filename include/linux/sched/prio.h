#ifndef _SCHED_PRIO_H
#define _SCHED_PRIO_H

#define MAX_NICE	19
#define MIN_NICE	-20
#define NICE_WIDTH	(MAX_NICE - MIN_NICE + 1)

/*
 * Priority of a process goes from 0..MAX_PRIO-1, valid RT
 * priority is 0..MAX_RT_PRIO-1, and SCHED_NORMAL/SCHED_BATCH
 * tasks are in the range MAX_RT_PRIO..MAX_PRIO-1. Priority
 * values are inverted: lower p->prio value means higher priority.
 *
 * The MAX_USER_RT_PRIO value allows the actual maximum
 * RT priority to be separate from the value exported to
 * user-space.  This allows kernel threads to set their
 * priority to a value higher than any user task. Note:
 * MAX_RT_PRIO must not be smaller than MAX_USER_RT_PRIO.
 */

#define MAX_USER_RT_PRIO	100
#define MAX_RT_PRIO		MAX_USER_RT_PRIO                    /* 100 */

#define MAX_PRIO		(MAX_RT_PRIO + NICE_WIDTH)          /* 140 */
#define DEFAULT_PRIO		(MAX_RT_PRIO + NICE_WIDTH / 2)  /* NICE_0 = 120 */

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to static priority [ MAX_RT_PRIO..MAX_PRIO-1 ],
 * and back.
 */
/* nice 的取值范围是            [ -20 ... 0 ... 19 ]
   静态优先级的取值范围是 [ -1            ... 139 ] */
/*********************************************************************************************************
** 函数名称: NICE_TO_PRIO
** 功能描述: 把用户指定的 nice 值转换成与其对应的静态优先级
** 输	 入: nice - 用户指定的 nice 值
** 输	 出: ret - 静态优先级
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define NICE_TO_PRIO(nice)	((nice) + DEFAULT_PRIO)

/*********************************************************************************************************
** 函数名称: PRIO_TO_NICE
** 功能描述: 把指定的静态优先级转换成与其对应的用户级 nice 值
** 输	 入: prio - 指定的静态优先级
** 输	 出: ret - 用户级 nice 值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define PRIO_TO_NICE(prio)	((prio) - DEFAULT_PRIO)

/*
 * 'User priority' is the nice value converted to something we
 * can work with better when scaling various scheduler parameters,
 * it's a [ 0 ... 39 ] range.
 */
/* 用户级优先级的取值范围是 [ 0            ... 39 ] */
/*********************************************************************************************************
** 函数名称: USER_PRIO
** 功能描述: 把指定的 cfs 任务的静态优先级转换成与其对应的用户优先级
** 输	 入: prio - 指定的 cfs 任务的优先级
** 输	 出: ret - 对应的用户优先级
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define USER_PRIO(p)		((p)-MAX_RT_PRIO)

/*********************************************************************************************************
** 函数名称: USER_PRIO
** 功能描述: 获取和指定的 cfs 任务对应的用户优先级
** 输	 入: p - 指定的 cfs 任务指针
** 输	 出: ret - 对应的用户优先级
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define TASK_USER_PRIO(p)	USER_PRIO((p)->static_prio)

#define MAX_USER_PRIO		(USER_PRIO(MAX_PRIO)) /* 40 */

/*
 * Convert nice value [19,-20] to rlimit style value [1,40].
 */
static inline long nice_to_rlimit(long nice)
{
	return (MAX_NICE - nice + 1);
}

/*
 * Convert rlimit style value [1,40] to nice value [-20, 19].
 */
static inline long rlimit_to_nice(long prio)
{
	return (MAX_NICE - prio + 1);
}

#endif /* _SCHED_PRIO_H */
