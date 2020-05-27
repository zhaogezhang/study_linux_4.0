/*
 * sched_clock.c: support for extending counters to full 64-bit ns counter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <linux/hrtimer.h>
#include <linux/sched_clock.h>
#include <linux/seqlock.h>
#include <linux/bitops.h>

struct clock_data {
	ktime_t wrap_kt;    /* 表示当前调度时钟模块经历多长时间计数值会溢出 */
	u64 epoch_ns;       /* 表示上一次更新调度时钟时、上电后系统经历的时间，单位为 ns，详情见 update_sched_clock 函数 */
	u64 epoch_cyc;      /* 表示上一次更新调度时钟时、上电后系统经历的时间，单位为一个时钟周期，详情见 update_sched_clock 函数 */
	seqcount_t seq;     
	unsigned long rate; /* 表示当前调度时钟模块的时钟频率 */
	u32 mult;           /* 表示一个 HZ 对应多少个 ns */
	u32 shift;          /* 表示当前调度时钟的转换精度位数，详情见 cyc_to_ns 函数 */
	bool suspended;     /* 表示当前调度时钟是否被挂起，详情见 sched_clock_suspend 函数 */
};

static struct hrtimer sched_clock_timer;
static int irqtime = -1;

core_param(irqtime, irqtime, int, 0400);

/* 表示和当前调度时钟相关的数据 */
static struct clock_data cd = {
	.mult	= NSEC_PER_SEC / HZ,
};

/* 表示当前使用的调度时钟计数值的 bits 数 */
static u64 __read_mostly sched_clock_mask;

/*********************************************************************************************************
** 函数名称: jiffy_sched_clock_read
** 功能描述: 获取当前系统从上电开始一共经历的 jiffies 周期数
** 输	 入: 
** 输	 出: u64 - 经历的 jiffies 周期数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 notrace jiffy_sched_clock_read(void)
{
	/*
	 * We don't need to use get_jiffies_64 on 32-bit arches here
	 * because we register with BITS_PER_LONG
	 */
	return (u64)(jiffies - INITIAL_JIFFIES);
}

/*********************************************************************************************************
** 函数名称: jiffy_sched_clock_read
** 功能描述: 获取当前系统从上电开始一共经历的 jiffies 周期数
** 输	 入: 
** 输	 出: u64 - 经历的 jiffies 周期数
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static u64 __read_mostly (*read_sched_clock)(void) = jiffy_sched_clock_read;

/*********************************************************************************************************
** 函数名称: cyc_to_ns
** 功能描述: 根据指定的参数将指定的 jiffies 周期数转换成与其对应的时间，单位是 ns
** 输	 入: cyc - 指定的 jiffies 周期数
**         : mult - 指定的乘积因子
**         : shift - 指定的转换精度位数
** 输	 出: u64 - 对应的时间，单位是 ns
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline u64 notrace cyc_to_ns(u64 cyc, u32 mult, u32 shift)
{
	return (cyc * mult) >> shift;
}

/*********************************************************************************************************
** 函数名称: sched_clock
** 功能描述: 通过高精度定时器计算出当前调度系统时间，单位是纳秒
** 输	 入: 
** 输	 出: unsigned long long - 当前调度系统时间
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
unsigned long long notrace sched_clock(void)
{
	u64 epoch_ns;
	u64 epoch_cyc;
	u64 cyc;
	unsigned long seq;

	if (cd.suspended)
		return cd.epoch_ns;

	do {
		seq = raw_read_seqcount_begin(&cd.seq);
		epoch_cyc = cd.epoch_cyc;
		epoch_ns = cd.epoch_ns;
	} while (read_seqcount_retry(&cd.seq, seq));

	cyc = read_sched_clock();
	cyc = (cyc - epoch_cyc) & sched_clock_mask;
	return epoch_ns + cyc_to_ns(cyc, cd.mult, cd.shift);
}

/*
 * Atomically update the sched_clock epoch.
 */
/*********************************************************************************************************
** 函数名称: update_sched_clock
** 功能描述: 根据 read_sched_clock 时钟值同步更新当前调度时钟模块的时间信息
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void notrace update_sched_clock(void)
{
	unsigned long flags;
	u64 cyc;
	u64 ns;

	cyc = read_sched_clock();
	ns = cd.epoch_ns +
		cyc_to_ns((cyc - cd.epoch_cyc) & sched_clock_mask,
			  cd.mult, cd.shift);

	raw_local_irq_save(flags);
	raw_write_seqcount_begin(&cd.seq);
	cd.epoch_ns = ns;
	cd.epoch_cyc = cyc;
	raw_write_seqcount_end(&cd.seq);
	raw_local_irq_restore(flags);
}

/*********************************************************************************************************
** 函数名称: sched_clock_poll
** 功能描述: 更新当前调度时钟模块信息并重新启动调度时钟的高精度定时器
** 输	 入: hrt - 当前调度时钟模块的高精度定时器指针
** 输	 出: HRTIMER_RESTART - 表示重新启动了高精度定时器
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static enum hrtimer_restart sched_clock_poll(struct hrtimer *hrt)
{
	update_sched_clock();
	hrtimer_forward_now(hrt, cd.wrap_kt);
	return HRTIMER_RESTART;
}

/*********************************************************************************************************
** 函数名称: sched_clock_register
** 功能描述: 根据函数指定的参数尝试注册一个新的调度时钟
** 输	 入: read - 新的调度时钟的读函数指针
**         : bits - 指定的调度时钟计数值的 bits 数
**         : rate - 新的时钟频率
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __init sched_clock_register(u64 (*read)(void), int bits,
				 unsigned long rate)
{
	u64 res, wrap, new_mask, new_epoch, cyc, ns;
	u32 new_mult, new_shift;
	ktime_t new_wrap_kt;
	unsigned long r;
	char r_unit;

    /* 如果新注册的调度时钟精度更高，则更新调度时钟模块使用新的调度时钟 */
	if (cd.rate > rate)
		return;

	WARN_ON(!irqs_disabled());

	/* calculate the mult/shift to convert counter ticks to ns. */
	clocks_calc_mult_shift(&new_mult, &new_shift, rate, NSEC_PER_SEC, 3600);

	new_mask = CLOCKSOURCE_MASK(bits);

	/* calculate how many ns until we wrap */
	wrap = clocks_calc_max_nsecs(new_mult, new_shift, 0, new_mask);
	new_wrap_kt = ns_to_ktime(wrap - (wrap >> 3));

	/* update epoch for new counter and update epoch_ns from old counter*/
	new_epoch = read();
	cyc = read_sched_clock();
	ns = cd.epoch_ns + cyc_to_ns((cyc - cd.epoch_cyc) & sched_clock_mask,
			  cd.mult, cd.shift);

	raw_write_seqcount_begin(&cd.seq);
	read_sched_clock = read;
	sched_clock_mask = new_mask;
	cd.rate = rate;
	cd.wrap_kt = new_wrap_kt;
	cd.mult = new_mult;
	cd.shift = new_shift;
	cd.epoch_cyc = new_epoch;
	cd.epoch_ns = ns;
	raw_write_seqcount_end(&cd.seq);

	r = rate;
	if (r >= 4000000) {
		r /= 1000000;
		r_unit = 'M';
	} else if (r >= 1000) {
		r /= 1000;
		r_unit = 'k';
	} else
		r_unit = ' ';

	/* calculate the ns resolution of this counter */
	res = cyc_to_ns(1ULL, new_mult, new_shift);

	pr_info("sched_clock: %u bits at %lu%cHz, resolution %lluns, wraps every %lluns\n",
		bits, r, r_unit, res, wrap);

	/* Enable IRQ time accounting if we have a fast enough sched_clock */
	if (irqtime > 0 || (irqtime == -1 && rate >= 1000000))
		enable_sched_clock_irqtime();

	pr_debug("Registered %pF as sched_clock source\n", read);
}

/*********************************************************************************************************
** 函数名称: sched_clock_postinit
** 功能描述: 注册默认的调度时钟并同步更新当前调度时钟模块的时钟信息，然后初始化并启动当前调度时钟
**         : 模块的高精度定时器
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __init sched_clock_postinit(void)
{
	/*
	 * If no sched_clock function has been provided at that point,
	 * make it the final one one.
	 */
	if (read_sched_clock == jiffy_sched_clock_read)
		sched_clock_register(jiffy_sched_clock_read, BITS_PER_LONG, HZ);

	update_sched_clock();

	/*
	 * Start the timer to keep sched_clock() properly updated and
	 * sets the initial epoch.
	 */
	hrtimer_init(&sched_clock_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sched_clock_timer.function = sched_clock_poll;
	hrtimer_start(&sched_clock_timer, cd.wrap_kt, HRTIMER_MODE_REL);
}

/*********************************************************************************************************
** 函数名称: sched_clock_suspend
** 功能描述: 挂起当前的调度时钟模块
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int sched_clock_suspend(void)
{
	update_sched_clock();
	hrtimer_cancel(&sched_clock_timer);
	cd.suspended = true;
	return 0;
}

/*********************************************************************************************************
** 函数名称: sched_clock_suspend
** 功能描述: 恢复当前的调度时钟模块
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void sched_clock_resume(void)
{
	cd.epoch_cyc = read_sched_clock();
	hrtimer_start(&sched_clock_timer, cd.wrap_kt, HRTIMER_MODE_REL);
	cd.suspended = false;
}

static struct syscore_ops sched_clock_ops = {
	.suspend = sched_clock_suspend,
	.resume = sched_clock_resume,
};

/*********************************************************************************************************
** 函数名称: sched_clock_syscore_init
** 功能描述: 注册当前调度时钟模块的挂起和恢复操作函数指针
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int __init sched_clock_syscore_init(void)
{
	register_syscore_ops(&sched_clock_ops);
	return 0;
}
device_initcall(sched_clock_syscore_init);
