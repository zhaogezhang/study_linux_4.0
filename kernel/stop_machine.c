/*
 * kernel/stop_machine.c
 *
 * Copyright (C) 2008, 2005	IBM Corporation.
 * Copyright (C) 2008, 2005	Rusty Russell rusty@rustcorp.com.au
 * Copyright (C) 2010		SUSE Linux Products GmbH
 * Copyright (C) 2010		Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2 and any later version.
 */
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/stop_machine.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/smpboot.h>
#include <linux/atomic.h>
#include <linux/lglock.h>

/*
 * Structure to determine completion condition and record errors.  May
 * be shared by works on different cpus.
 */
struct cpu_stop_done {
	atomic_t		nr_todo;	/* nr left to execute */
	bool			executed;	/* actually executed? */
	int			ret;		/* collected return value */
	struct completion	completion;	/* fired if nr_todo reaches 0 */
};

/* the actual stopper, one per every possible cpu, enabled on online cpus */
struct cpu_stopper {
	spinlock_t		lock;
	bool			enabled;	/* is this stopper enabled? */
	struct list_head	works;		/* list of pending works */
};

/* 在每个 cpu 上用来记录待执行的 cpu stop 工作信息 */
static DEFINE_PER_CPU(struct cpu_stopper, cpu_stopper);

/* 定义了每个 cpu 上用来执行 cpu stop 工作的任务指针，即实际的 cpu stop 任务 */
static DEFINE_PER_CPU(struct task_struct *, cpu_stopper_task);

static bool stop_machine_initialized = false;

/*
 * Avoids a race between stop_two_cpus and global stop_cpus, where
 * the stoppers could get queued up in reverse order, leading to
 * system deadlock. Using an lglock means stop_two_cpus remains
 * relatively cheap.
 */
DEFINE_STATIC_LGLOCK(stop_cpus_lock);

/*********************************************************************************************************
** 函数名称: cpu_stop_init_done
** 功能描述: 根据函数参数初始化指定的 cpu stop done 结构
** 输	 入: nr_todo - 指定需要执行 stop 工作的 cpu 个数
** 输	 出: done - 初始化完的 cpu stop done 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stop_init_done(struct cpu_stop_done *done, unsigned int nr_todo)
{
	memset(done, 0, sizeof(*done));
	atomic_set(&done->nr_todo, nr_todo);
	init_completion(&done->completion);
}

/* signal completion unless @done is NULL */
/*********************************************************************************************************
** 函数名称: cpu_stop_signal_done
** 功能描述: 在某个 cpu 执行完指定的 cpu stop 工作后调用这个函数用来同步更新执行状态
** 输	 入: done - 指定的 cpu stop done 结构指针
**         : executed - 是否成功执行
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stop_signal_done(struct cpu_stop_done *done, bool executed)
{
	if (done) {
		if (executed)
			done->executed = true;

		/* 如果所有的 cpu 都执行完了指定的 cpu stop 工作，则发送指定的条件变量 */
		if (atomic_dec_and_test(&done->nr_todo))
			complete(&done->completion);
	}
}

/* queue @work to @stopper.  if offline, @work is completed immediately */
/*********************************************************************************************************
** 函数名称: cpu_stop_queue_work
** 功能描述: 尝试把指定的 cpu stop 工作添加到指定 cpu 上并唤醒对应的 stop 任务去执行这个工作
** 输	 入: cpu - 指定的 cpu id 值
**         : work - 指定的 cpu stop 工作
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stop_queue_work(unsigned int cpu, struct cpu_stop_work *work)
{
	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
	struct task_struct *p = per_cpu(cpu_stopper_task, cpu);

	unsigned long flags;

	spin_lock_irqsave(&stopper->lock, flags);

	if (stopper->enabled) {
		list_add_tail(&work->list, &stopper->works);
		wake_up_process(p);
	} else
		cpu_stop_signal_done(work->done, false);

	spin_unlock_irqrestore(&stopper->lock, flags);
}

/**
 * stop_one_cpu - stop a cpu
 * @cpu: cpu to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Execute @fn(@arg) on @cpu.  @fn is run in a process context with
 * the highest priority preempting any task on the cpu and
 * monopolizing it.  This function returns after the execution is
 * complete.
 *
 * This function doesn't guarantee @cpu stays online till @fn
 * completes.  If @cpu goes down in the middle, execution may happen
 * partially or fully on different cpus.  @fn should either be ready
 * for that or the caller should ensure that @cpu stays online until
 * this function completes.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * -ENOENT if @fn(@arg) was not executed because @cpu was offline;
 * otherwise, the return value of @fn.
 */
/*********************************************************************************************************
** 函数名称: stop_one_cpu
** 功能描述: 把指定的 stop 工作添加到指定 cpu 上并通过条件变量等待其运行完成
** 输	 入: cpu - 指定的 cpu id 值
**         : fn - 指定的 stop 工作函数指针
**         : arg - 指定的 stop 工作函数参数指针
** 输	 出: done.ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg)
{
	struct cpu_stop_done done;
	struct cpu_stop_work work = { .fn = fn, .arg = arg, .done = &done };

	cpu_stop_init_done(&done, 1);
	cpu_stop_queue_work(cpu, &work);
	wait_for_completion(&done.completion);
	return done.executed ? done.ret : -ENOENT;
}

/* This controls the threads on each CPU. */
enum multi_stop_state {
	/* Dummy starting state for thread. */
	MULTI_STOP_NONE,
	/* Awaiting everyone to be scheduled. */
	MULTI_STOP_PREPARE,
	/* Disable interrupts. */
	MULTI_STOP_DISABLE_IRQ,
	/* Run the function */
	MULTI_STOP_RUN,
	/* Exit */
	MULTI_STOP_EXIT,
};

struct multi_stop_data {
	int			(*fn)(void *);
	void			*data;
	
	/* Like num_online_cpus(), but hotplug cpu uses us, so we need this. */
	/* 表示当前一共有多少个 cpu 在执行 stop 任务 */
	unsigned int		num_threads;

	/* 表示当前处于 active 状态且可以用来执行 stop 任务的 cpu 掩码值，详情见 stop_two_cpus 函数 */
	const struct cpumask	*active_cpus;

	enum multi_stop_state	state;
	
	/* 表示当前还有多少个 cpu 的 stop 任务没执行完 */
	atomic_t		thread_ack;
};

/*********************************************************************************************************
** 函数名称: set_state
** 功能描述: 把指定的 multi stop 状态机设置为指定的新状态
** 输	 入: msdata - 指定的 multi stop 数据指针
**         : newstate - 指定的新状态 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void set_state(struct multi_stop_data *msdata,
		      enum multi_stop_state newstate)
{
	/* Reset ack counter. */
	atomic_set(&msdata->thread_ack, msdata->num_threads);
	smp_wmb();
	msdata->state = newstate;
}

/* Last one to ack a state moves to the next state. */
/*********************************************************************************************************
** 函数名称: ack_state
** 功能描述: 在一个 cpu 执行完 stop 任务后调用这个函数来同步更新状态数据
** 输	 入: msdata - 指定的 multi stop 数据指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void ack_state(struct multi_stop_data *msdata)
{
    /* 如果在当前状态下所有 cpu 的 stop 任务都执行完成，则进入到下一个状态 */
	if (atomic_dec_and_test(&msdata->thread_ack))
		set_state(msdata, msdata->state + 1);
}

/* This is the cpu_stop function which stops the CPU. */
/*********************************************************************************************************
** 函数名称: multi_cpu_stop
** 功能描述: 当我们执行的是多个 cpu 的 stop 工作时，通过调用这个函数实现
** 输	 入: data - 指定的 multi stop 数据指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int multi_cpu_stop(void *data)
{
	struct multi_stop_data *msdata = data;
	enum multi_stop_state curstate = MULTI_STOP_NONE;
	int cpu = smp_processor_id(), err = 0;
	unsigned long flags;
	bool is_active;

	/*
	 * When called from stop_machine_from_inactive_cpu(), irq might
	 * already be disabled.  Save the state and restore it on exit.
	 */
	local_save_flags(flags);

	if (!msdata->active_cpus)
		is_active = cpu == cpumask_first(cpu_online_mask);
	else
		is_active = cpumask_test_cpu(cpu, msdata->active_cpus);

	/* Simple state machine */
	do {
		/* Chill out and ensure we re-read multi_stop_state. */
		cpu_relax();

	    /* 当所有 cpu 在当前状态下的工作都执行完成后再一起更新到下一个状态继续执行 */
		if (msdata->state != curstate) {
			curstate = msdata->state;
			switch (curstate) {
			case MULTI_STOP_DISABLE_IRQ:
				local_irq_disable();
				hard_irq_disable();
				break;

		    /* 只在指定的某个 cpu 上执行 stop 工作 */
			case MULTI_STOP_RUN:
				if (is_active)
					err = msdata->fn(msdata->data);
				break;
			default:
				break;
			}
			ack_state(msdata);
		}
	} while (curstate != MULTI_STOP_EXIT);

	local_irq_restore(flags);
	return err;
}

struct irq_cpu_stop_queue_work_info {
	int cpu1;
	int cpu2;
	struct cpu_stop_work *work1;
	struct cpu_stop_work *work2;
};

/*
 * This function is always run with irqs and preemption disabled.
 * This guarantees that both work1 and work2 get queued, before
 * our local migrate thread gets the chance to preempt us.
 */
/*********************************************************************************************************
** 函数名称: irq_cpu_stop_queue_work
** 功能描述: 尝试把指定的 irq cpu stop 工作添加到对应的 cpu 上并唤醒对应的 stop 任务去执行这个工作
** 注     释: 这个函数需要在关闭中断和关闭抢占的情况下调用
** 输	 入: arg - 指定的 irq cpu stop 工作队列信息指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void irq_cpu_stop_queue_work(void *arg)
{
	struct irq_cpu_stop_queue_work_info *info = arg;
	cpu_stop_queue_work(info->cpu1, info->work1);
	cpu_stop_queue_work(info->cpu2, info->work2);
}

/**
 * stop_two_cpus - stops two cpus
 * @cpu1: the cpu to stop
 * @cpu2: the other cpu to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Stops both the current and specified CPU and runs @fn on one of them.
 *
 * returns when both are completed.
 */
/*********************************************************************************************************
** 函数名称: stop_two_cpus
** 功能描述: 在关闭中断的情况下，在指定 cpu 中的某个 cpu 上“同步”执行指定的工作函数
** 注     释: “同步”执行并不是指在每个 cpu 上都执行指定的工作函数，而是使这几个 cpu 同步经历指定的状态机
**         : 状态，但是只在一个 cpu 上执行指定的工作函数，其他 cpu 只是同步的进行状态过渡
** 输	 入: cpu1 - 指定的第一个 cpu id 值
**         : cpu2 - 指定的第二个 cpu id 值
**         : fn - 指定的需要运行的函数指针
**         : arg - 指定的函数参数
** 输	 出: done.ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int stop_two_cpus(unsigned int cpu1, unsigned int cpu2, cpu_stop_fn_t fn, void *arg)
{
	struct cpu_stop_done done;
	struct cpu_stop_work work1, work2;
	struct irq_cpu_stop_queue_work_info call_args;
	struct multi_stop_data msdata;

	preempt_disable();
	
	msdata = (struct multi_stop_data){
		.fn = fn,
		.data = arg,
		.num_threads = 2,
		
		/* 因为 stop 任务只需要在一个 cpu 上执行，所以我们默认选择在第一个 cpu 上执行 */
		.active_cpus = cpumask_of(cpu1),
	};

	work1 = work2 = (struct cpu_stop_work){
		.fn = multi_cpu_stop,
		.arg = &msdata,
		.done = &done
	};

	call_args = (struct irq_cpu_stop_queue_work_info){
		.cpu1 = cpu1,
		.cpu2 = cpu2,
		.work1 = &work1,
		.work2 = &work2,
	};

	cpu_stop_init_done(&done, 2);
	set_state(&msdata, MULTI_STOP_PREPARE);

	/*
	 * If we observe both CPUs active we know _cpu_down() cannot yet have
	 * queued its stop_machine works and therefore ours will get executed
	 * first. Or its not either one of our CPUs that's getting unplugged,
	 * in which case we don't care.
	 *
	 * This relies on the stopper workqueues to be FIFO.
	 */
	if (!cpu_active(cpu1) || !cpu_active(cpu2)) {
		preempt_enable();
		return -ENOENT;
	}

	lg_local_lock(&stop_cpus_lock);
	
	/*
	 * Queuing needs to be done by the lowest numbered CPU, to ensure
	 * that works are always queued in the same order on every CPU.
	 * This prevents deadlocks.
	 */
	smp_call_function_single(min(cpu1, cpu2),
				 &irq_cpu_stop_queue_work,
				 &call_args, 1);
	
	lg_local_unlock(&stop_cpus_lock);
	preempt_enable();

	wait_for_completion(&done.completion);

	return done.executed ? done.ret : -ENOENT;
}

/**
 * stop_one_cpu_nowait - stop a cpu but don't wait for completion
 * @cpu: cpu to stop
 * @fn: function to execute
 * @arg: argument to @fn
 * @work_buf: pointer to cpu_stop_work structure
 *
 * Similar to stop_one_cpu() but doesn't wait for completion.  The
 * caller is responsible for ensuring @work_buf is currently unused
 * and will remain untouched until stopper starts executing @fn.
 *
 * CONTEXT:
 * Don't care.
 */
/*********************************************************************************************************
** 函数名称: cpu_stop_queue_work
** 功能描述: 尝试把指定的 cpu stop 工作添加到指定 cpu 上并唤醒对应的 stop 任务去执行这个工作
** 注     释: 这个函数不会通过条件变量同步等待指定的工作执行完成在返回，而是立即返回
** 输	 入: cpu - 指定的 cpu id 值
**         : fn - 指定的需要运行的函数指针
**         : arg - 指定的函数参数
**         : work_buf - 指定的 cpu stop 工作描述缓冲区地址
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void stop_one_cpu_nowait(unsigned int cpu, cpu_stop_fn_t fn, void *arg,
			struct cpu_stop_work *work_buf)
{
	*work_buf = (struct cpu_stop_work){ .fn = fn, .arg = arg, };
	cpu_stop_queue_work(cpu, work_buf);
}

/* static data for stop_cpus */
static DEFINE_MUTEX(stop_cpus_mutex);
static DEFINE_PER_CPU(struct cpu_stop_work, stop_cpus_work);

/*********************************************************************************************************
** 函数名称: queue_stop_cpus_work
** 功能描述: 向指定的 cpu 位图表示的每一个 cpu 上都添加一个指定的 stop 工作函数并唤醒对应的 
**         : stop 任务去执行这个工作
** 注     释: 这个函数不会通过条件变量同步等待指定的工作执行完成在返回，而是立即返回
** 输	 入: cpumask - 指定的 cpu 位图掩码值
**         : fn - 指定的需要运行的函数指针
**         : arg - 指定的函数参数
**         : done - 指定的 cpu stop done 结构指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void queue_stop_cpus_work(const struct cpumask *cpumask,
				 cpu_stop_fn_t fn, void *arg,
				 struct cpu_stop_done *done)
{
	struct cpu_stop_work *work;
	unsigned int cpu;

	/* initialize works and done */
	for_each_cpu(cpu, cpumask) {
		work = &per_cpu(stop_cpus_work, cpu);
		work->fn = fn;
		work->arg = arg;
		work->done = done;
	}

	/*
	 * Disable preemption while queueing to avoid getting
	 * preempted by a stopper which might wait for other stoppers
	 * to enter @fn which can lead to deadlock.
	 */
	lg_global_lock(&stop_cpus_lock);
	for_each_cpu(cpu, cpumask)
		cpu_stop_queue_work(cpu, &per_cpu(stop_cpus_work, cpu));
	lg_global_unlock(&stop_cpus_lock);
}

/*********************************************************************************************************
** 函数名称: __stop_cpus
** 功能描述: 向指定的 cpu 位图表示的每一个 cpu 上都添加一个指定的 stop 工作函数并等待它们执行完成
** 输	 入: cpumask - 指定的 cpu 位图掩码值
**         : fn - 指定的需要运行的函数指针
**         : arg - 指定的函数参数
** 输	 出: done.ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int __stop_cpus(const struct cpumask *cpumask,
		       cpu_stop_fn_t fn, void *arg)
{
	struct cpu_stop_done done;

	cpu_stop_init_done(&done, cpumask_weight(cpumask));
	queue_stop_cpus_work(cpumask, fn, arg, &done);
	wait_for_completion(&done.completion);
	return done.executed ? done.ret : -ENOENT;
}

/**
 * stop_cpus - stop multiple cpus
 * @cpumask: cpus to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Execute @fn(@arg) on online cpus in @cpumask.  On each target cpu,
 * @fn is run in a process context with the highest priority
 * preempting any task on the cpu and monopolizing it.  This function
 * returns after all executions are complete.
 *
 * This function doesn't guarantee the cpus in @cpumask stay online
 * till @fn completes.  If some cpus go down in the middle, execution
 * on the cpu may happen partially or fully on different cpus.  @fn
 * should either be ready for that or the caller should ensure that
 * the cpus stay online until this function completes.
 *
 * All stop_cpus() calls are serialized making it safe for @fn to wait
 * for all cpus to start executing it.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * -ENOENT if @fn(@arg) was not executed at all because all cpus in
 * @cpumask were offline; otherwise, 0 if all executions of @fn
 * returned 0, any non zero return value if any returned non zero.
 */
/*********************************************************************************************************
** 函数名称: __stop_cpus
** 功能描述: 向指定的 cpu 位图表示的每一个 cpu 上都添加一个指定的 stop 工作函数并等待它们执行完成
** 输	 入: cpumask - 指定的 cpu 位图掩码值
**         : fn - 指定的需要运行的函数指针
**         : arg - 指定的函数参数
** 输	 出: done.ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int stop_cpus(const struct cpumask *cpumask, cpu_stop_fn_t fn, void *arg)
{
	int ret;

	/* static works are used, process one request at a time */
	mutex_lock(&stop_cpus_mutex);
	ret = __stop_cpus(cpumask, fn, arg);
	mutex_unlock(&stop_cpus_mutex);
	return ret;
}

/**
 * try_stop_cpus - try to stop multiple cpus
 * @cpumask: cpus to stop
 * @fn: function to execute
 * @arg: argument to @fn
 *
 * Identical to stop_cpus() except that it fails with -EAGAIN if
 * someone else is already using the facility.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * -EAGAIN if someone else is already stopping cpus, -ENOENT if
 * @fn(@arg) was not executed at all because all cpus in @cpumask were
 * offline; otherwise, 0 if all executions of @fn returned 0, any non
 * zero return value if any returned non zero.
 */
/*********************************************************************************************************
** 函数名称: try_stop_cpus
** 功能描述: 尝试向指定的 cpu 位图表示的每一个 cpu 上都添加一个指定的 stop 工作函数并等待它们执行完成
** 输	 入: cpumask - 指定的 cpu 位图掩码值
**         : fn - 指定的需要运行的函数指针
**         : arg - 指定的函数参数
** 输	 出: done.ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
**         : -EAGAIN - 执行失败需要重新尝试
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int try_stop_cpus(const struct cpumask *cpumask, cpu_stop_fn_t fn, void *arg)
{
	int ret;

	/* static works are used, process one request at a time */
	if (!mutex_trylock(&stop_cpus_mutex))
		return -EAGAIN;
	ret = __stop_cpus(cpumask, fn, arg);
	mutex_unlock(&stop_cpus_mutex);
	return ret;
}

/*********************************************************************************************************
** 函数名称: cpu_stop_should_run
** 功能描述: 判断指定的 cpu 上是否有待运行的 stop 工作函数
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: 1 - 有待运行的 stop 工作函数
**         : 0 - 没有待运行的 stop 工作函数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int cpu_stop_should_run(unsigned int cpu)
{
	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
	unsigned long flags;
	int run;

	spin_lock_irqsave(&stopper->lock, flags);
	run = !list_empty(&stopper->works);
	spin_unlock_irqrestore(&stopper->lock, flags);
	return run;
}

/*********************************************************************************************************
** 函数名称: cpu_stopper_thread
** 功能描述: 表示指定 cpu 上用来运行 stop 工作函数任务实现
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stopper_thread(unsigned int cpu)
{
	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
	struct cpu_stop_work *work;
	int ret;

repeat:
	
	/* 尝试从指定的 cpu 上获取一个待运行的 stop 工作函数 */
	work = NULL;
	spin_lock_irq(&stopper->lock);
	if (!list_empty(&stopper->works)) {
		work = list_first_entry(&stopper->works,
					struct cpu_stop_work, list);
		list_del_init(&work->list);
	}
	spin_unlock_irq(&stopper->lock);

    /* 如果成功获取到一个待运行的 stop 工作函数则直接运行它 */
	if (work) {
		cpu_stop_fn_t fn = work->fn;
		void *arg = work->arg;
		struct cpu_stop_done *done = work->done;
		char ksym_buf[KSYM_NAME_LEN] __maybe_unused;

		/* cpu stop callbacks are not allowed to sleep */
		preempt_disable();

		ret = fn(arg);
		if (ret)
			done->ret = ret;

		/* restore preemption and check it's still balanced */
		preempt_enable();
		WARN_ONCE(preempt_count(),
			  "cpu_stop: %s(%p) leaked preempt count\n",
			  kallsyms_lookup((unsigned long)fn, NULL, NULL, NULL,
					  ksym_buf), arg);

		cpu_stop_signal_done(done, true);
		goto repeat;
	}
}

extern void sched_set_stop_task(int cpu, struct task_struct *stop);

/*********************************************************************************************************
** 函数名称: cpu_stop_create
** 功能描述: 设置指定 cpu 上的 stop 任务函数
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stop_create(unsigned int cpu)
{
	sched_set_stop_task(cpu, per_cpu(cpu_stopper_task, cpu));
}

/*********************************************************************************************************
** 函数名称: cpu_stop_park
** 功能描述: 关闭指定 cpu 上的 stop 任务函数功能
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stop_park(unsigned int cpu)
{
	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
	struct cpu_stop_work *work;
	unsigned long flags;

	/* drain remaining works */
	spin_lock_irqsave(&stopper->lock, flags);
	list_for_each_entry(work, &stopper->works, list)
		cpu_stop_signal_done(work->done, false);
	stopper->enabled = false;
	spin_unlock_irqrestore(&stopper->lock, flags);
}

/*********************************************************************************************************
** 函数名称: cpu_stop_park
** 功能描述: 开启指定 cpu 上的 stop 任务函数功能
** 输	 入: cpu - 指定的 cpu id 值
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void cpu_stop_unpark(unsigned int cpu)
{
	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);

	spin_lock_irq(&stopper->lock);
	stopper->enabled = true;
	spin_unlock_irq(&stopper->lock);
}

static struct smp_hotplug_thread cpu_stop_threads = {
	.store			= &cpu_stopper_task,
	.thread_should_run	= cpu_stop_should_run,
	.thread_fn		= cpu_stopper_thread,
	.thread_comm		= "migration/%u",
	.create			= cpu_stop_create,
	.setup			= cpu_stop_unpark,
	.park			= cpu_stop_park,
	.pre_unpark		= cpu_stop_unpark,
	.selfparking		= true,
};

/*********************************************************************************************************
** 函数名称: cpu_stop_init
** 功能描述: 初始当前系统的 stop 功能模块
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int __init cpu_stop_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);

		spin_lock_init(&stopper->lock);
		INIT_LIST_HEAD(&stopper->works);
	}

	BUG_ON(smpboot_register_percpu_thread(&cpu_stop_threads));
	stop_machine_initialized = true;
	return 0;
}
early_initcall(cpu_stop_init);

#ifdef CONFIG_STOP_MACHINE

/*********************************************************************************************************
** 函数名称: __stop_machine
** 功能描述: 如果当前系统已经初始化了 stop machine 功能模块，则尝试向每一个 online cpu 上都添加
**         : 一个指定的 stop 工作函数并等待它们执行完成，否则只在当前 cpu 上执行指定的 stop 工作函数
** 输	 入: fn - 指定的 stop 工作函数指针
**         : data - 指定的 stop 工作函数参数指针
**         : cpus - 指定的 cpu 位图掩码值
** 输	 出: ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int __stop_machine(int (*fn)(void *), void *data, const struct cpumask *cpus)
{
	struct multi_stop_data msdata = {
		.fn = fn,
		.data = data,
		.num_threads = num_online_cpus(),
		.active_cpus = cpus,
	};

	if (!stop_machine_initialized) {
		/*
		 * Handle the case where stop_machine() is called
		 * early in boot before stop_machine() has been
		 * initialized.
		 */
		unsigned long flags;
		int ret;

		WARN_ON_ONCE(msdata.num_threads != 1);

		local_irq_save(flags);
		hard_irq_disable();
		ret = (*fn)(data);
		local_irq_restore(flags);

		return ret;
	}

	/* Set the initial state and stop all online cpus. */
	set_state(&msdata, MULTI_STOP_PREPARE);
	return stop_cpus(cpu_online_mask, multi_cpu_stop, &msdata);
}

/*********************************************************************************************************
** 函数名称: stop_machine
** 功能描述: 如果当前系统已经初始化了 stop machine 功能模块，则尝试向每一个 online cpu 上都添加
**         : 一个指定的 stop 工作函数并等待它们执行完成，否则只在当前 cpu 上执行指定的 stop 工作函数
** 输	 入: fn - 指定的 stop 工作函数指针
**         : data - 指定的 stop 工作函数参数指针
**         : cpus - 指定的 cpu 位图掩码值
** 输	 出: ret - stop 工作返回值
**         : -ENOENT - 指定 cpu 上的 stop 任务没启动
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int stop_machine(int (*fn)(void *), void *data, const struct cpumask *cpus)
{
	int ret;

	/* No CPUs can come up or down during this. */
	get_online_cpus();
	ret = __stop_machine(fn, data, cpus);
	put_online_cpus();
	return ret;
}
EXPORT_SYMBOL_GPL(stop_machine);

/**
 * stop_machine_from_inactive_cpu - stop_machine() from inactive CPU
 * @fn: the function to run
 * @data: the data ptr for the @fn()
 * @cpus: the cpus to run the @fn() on (NULL = any online cpu)
 *
 * This is identical to stop_machine() but can be called from a CPU which
 * is not active.  The local CPU is in the process of hotplug (so no other
 * CPU hotplug can start) and not marked active and doesn't have enough
 * context to sleep.
 *
 * This function provides stop_machine() functionality for such state by
 * using busy-wait for synchronization and executing @fn directly for local
 * CPU.
 *
 * CONTEXT:
 * Local CPU is inactive.  Temporarily stops all active CPUs.
 *
 * RETURNS:
 * 0 if all executions of @fn returned 0, any non zero return value if any
 * returned non zero.
 */
/*********************************************************************************************************
** 函数名称: stop_machine_from_inactive_cpu
** 功能描述: 如果在 inactiv 状态的 cpu 上想要执行 stop 工作，则通过调用这个函数实现，常常在发生热插拔
**         : 的 cpu 上调用这个函数
** 输	 入: fn - 指定的 stop 工作函数指针
**         : data - 指定的 stop 工作函数参数指针
**         : cpus - 指定的 cpu 位图掩码值
** 输	 出: 0 - 所有 cpu 都返回 0
**         : other - 存在返回不是 0 的 cpu
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int stop_machine_from_inactive_cpu(int (*fn)(void *), void *data,
				  const struct cpumask *cpus)
{
	struct multi_stop_data msdata = { .fn = fn, .data = data,
					    .active_cpus = cpus };
	struct cpu_stop_done done;
	int ret;

	/* Local CPU must be inactive and CPU hotplug in progress. */
	BUG_ON(cpu_active(raw_smp_processor_id()));
	msdata.num_threads = num_active_cpus() + 1;	/* +1 for local */

	/* No proper task established and can't sleep - busy wait for lock. */
	while (!mutex_trylock(&stop_cpus_mutex))
		cpu_relax();

	/* Schedule work on other CPUs and execute directly for local CPU */
	set_state(&msdata, MULTI_STOP_PREPARE);
	cpu_stop_init_done(&done, num_active_cpus());
	queue_stop_cpus_work(cpu_active_mask, multi_cpu_stop, &msdata,
			     &done);
	ret = multi_cpu_stop(&msdata);

	/* Busy wait for completion. */
	while (!completion_done(&done.completion))
		cpu_relax();

	mutex_unlock(&stop_cpus_mutex);
	return ret ?: done.ret;
}

#endif	/* CONFIG_STOP_MACHINE */
