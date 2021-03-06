/*
 * Only give sleepers 50% of their service deficit. This allows
 * them to run sooner, but does not allow tons of sleepers to
 * rip the spread apart.
 */
/* 表示在把指定的调度实例添加到当前 cpu 正在运行的任务的左侧（在 cfs 红黑树上
   的位置）时，需要把虚拟时间的反向补偿值减半，这样可以保证新的调度实例及时得
   到调度并且不会对之前的任务的虚拟时间分配逻辑造成太大的影响，详情见 place_entity 函数 */
SCHED_FEAT(GENTLE_FAIR_SLEEPERS, true)

/*
 * Place new tasks ahead so that they do not starve already running
 * tasks
 */
/* 表示把指定的调度实例添加到当前 cpu 正在运行任务的右侧（在 cfs 红黑树上
   的位置），这样就不会马上抢占当前正在运行的调度实例了，详情见 place_entity 函数 */
SCHED_FEAT(START_DEBIT, true)

/*
 * Prefer to schedule the task we woke last (assuming it failed
 * wakeup-preemption), since its likely going to consume data we
 * touched, increases cache locality.
 */
SCHED_FEAT(NEXT_BUDDY, false)

/*
 * Prefer to schedule the task that ran last (when we did
 * wake-preempt) as that likely will touch the same data, increases
 * cache locality.
 */
SCHED_FEAT(LAST_BUDDY, true)

/*
 * Consider buddies to be cache hot, decreases the likelyness of a
 * cache buddy being migrated away, increases cache locality.
 */
/* 认为伙伴是缓存热点，减少了伙伴迁移的可能性，增加了缓存局部访问命中率 */
SCHED_FEAT(CACHE_HOT_BUDDY, true)

/*
 * Allow wakeup-time preemption of the current task:
 */
/* 表示允许被唤醒的新任务在被唤醒时抢占当前正在执行的任务，即抢占式唤醒 */
SCHED_FEAT(WAKEUP_PREEMPTION, true)

/*
 * Use arch dependent cpu capacity functions
 */
SCHED_FEAT(ARCH_CAPACITY, true)

SCHED_FEAT(HRTICK, false)
SCHED_FEAT(DOUBLE_TICK, false)
SCHED_FEAT(LB_BIAS, true)

/*
 * Decrement CPU capacity based on time not spent running tasks
 */
SCHED_FEAT(NONTASK_CAPACITY, true)

/*
 * Queue remote wakeups on the target CPU and process them
 * using the scheduler IPI. Reduces rq->lock contention/bounces.
 */
SCHED_FEAT(TTWU_QUEUE, true)

SCHED_FEAT(FORCE_SD_OVERLAP, false)
SCHED_FEAT(RT_RUNTIME_SHARE, true)
SCHED_FEAT(LB_MIN, false)

/*
 * Apply the automatic NUMA scheduling policy. Enabled automatically
 * at runtime if running on a NUMA machine. Can be controlled via
 * numa_balancing=
 */
#ifdef CONFIG_NUMA_BALANCING
SCHED_FEAT(NUMA,	false)

/*
 * NUMA_FAVOUR_HIGHER will favor moving tasks towards nodes where a
 * higher number of hinting faults are recorded during active load
 * balancing.
 */
/* 表示在 numa node 节点间进行任务迁移时，会把任务尽量迁移到之前发生了
   很多次 hinting faults 的 node 节点上，这样可以提高任务访问内存效率 */
SCHED_FEAT(NUMA_FAVOUR_HIGHER, true)

/*
 * NUMA_RESIST_LOWER will resist moving tasks towards nodes where a
 * lower number of hinting faults have been recorded. As this has
 * the potential to prevent a task ever migrating to a new node
 * due to CPU overload it is disabled by default.
 */
SCHED_FEAT(NUMA_RESIST_LOWER, false)
#endif
