#include <linux/init_task.h>
#include <linux/export.h>
#include <linux/mqueue.h>
#include <linux/sched.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/rt.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

/* 声明并初始化系统 0 号进程的信号信息 */
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);

/* 声明并初始化系统 0 号进程的信号处理信息 */
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);

/* Initial task structure */
/* 声明并初始化系统 0 号进程任务结构体信息 */
struct task_struct init_task = INIT_TASK(init_task);
EXPORT_SYMBOL(init_task);

/*
 * Initial thread structure. Alignment of this is handled by a special
 * linker map entry.
 */
/* 声明并初始化 0 号进程的线程信息结构体成员 */
union thread_union init_thread_union __init_task_data =
	{ INIT_THREAD_INFO(init_task) };
