/* Generic task switch macro wrapper, based on MN10300 definitions.
 *
 * It should be possible to use these on really simple architectures,
 * but it serves more as a starting point for new ports.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef __ASM_GENERIC_SWITCH_TO_H
#define __ASM_GENERIC_SWITCH_TO_H

#include <linux/thread_info.h>

/*
 * Context switching is now performed out-of-line in switch_to.S
 */
extern struct task_struct *__switch_to(struct task_struct *,
				       struct task_struct *);

/*********************************************************************************************************
** 函数名称: switch_to
** 功能描述: 从指定的任务上下文中切换到另一个指定的任务上下文中
** 输	 入: prev - 指定的被切换出的之前在运行的任务指针
**         : next - 指定的将要被切换进来的将要运行的任务指针
** 输	 出: last - 用来存储本次被切换出的任务指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define switch_to(prev, next, last)					\
	do {								\
		((last) = __switch_to((prev), (next)));			\
	} while (0)

#endif /* __ASM_GENERIC_SWITCH_TO_H */
