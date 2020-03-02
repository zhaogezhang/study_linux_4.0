/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002-2003 Nadia Yvette Chambers, IBM
 * (C) 2004 Nadia Yvette Chambers, Oracle
 * (C) 2002-2004 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 *
 * Pid namespaces:
 *    (C) 2007 Pavel Emelyanov <xemul@openvz.org>, OpenVZ, SWsoft Inc.
 *    (C) 2007 Sukadev Bhattiprolu <sukadev@us.ibm.com>, IBM
 *     Many thanks to Oleg Nesterov for comments and help
 *
 */

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/bootmem.h>
#include <linux/hash.h>
#include <linux/pid_namespace.h>
#include <linux/init_task.h>
#include <linux/syscalls.h>
#include <linux/proc_ns.h>
#include <linux/proc_fs.h>

/*********************************************************************************************************
** 函数名称: pid_hashfn
** 功能描述: 计算指定的 pid 的散列哈希值
** 输	 入: nr - 指定的 pid
**         : ns - 指定的 pid 的 namespace
** 输	 出: return - 得到的哈希值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define pid_hashfn(nr, ns)	\
	hash_long((unsigned long)nr + (unsigned long)ns, pidhash_shift)

/* 指定了当前系统内 pid 散列哈希数组地址 */
static struct hlist_head *pid_hash;

/* 声明并初始化 pid hash 表的长度位移数，所以当前系统的 pid hash 表长度为 2^4 = 16 */
static unsigned int pidhash_shift = 4;

/* 声明并初始化系统 0 号进程的 STRUCT_PID 信息 */
struct pid init_struct_pid = INIT_STRUCT_PID;

int pid_max = PID_MAX_DEFAULT;

#define RESERVED_PIDS		300

int pid_max_min = RESERVED_PIDS + 1;
int pid_max_max = PID_MAX_LIMIT;

/*********************************************************************************************************
** 函数名称: mk_pid
** 功能描述: 通过函数指定的参数生成一个 pid 值
** 输	 入: pid_ns - 指定的 pid namespace 指针
**         : map - 指定的 pid 的所在的 pidmap 数组成员结构指针
**         : off - 指定的 pid 在指定的 pidmap 数组成员中的偏移量
** 输	 出: int - 得到的 pid 数值
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline int mk_pid(struct pid_namespace *pid_ns,
		struct pidmap *map, int off)
{
	return (map - pid_ns->pidmap)*BITS_PER_PAGE + off;
}

/*********************************************************************************************************
** 函数名称: find_next_offset
** 功能描述: 从指定的 pidmap 的指定偏移位处开始查找下一个为 0 的 bit 位
** 输	 入: map - 指定的 pidmap 指针
**         : offset - 起始 bit 偏移量
** 输	 出: long - 查找到的 0 bit 的偏移位
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
#define find_next_offset(map, off)					\
		find_next_zero_bit((map)->page, BITS_PER_PAGE, off)

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */ 
/* 声明并初始化系统 0 号进程的 pid_namespace 信息 */
struct pid_namespace init_pid_ns = {
	.kref = {
		.refcount       = ATOMIC_INIT(2),
	},
	.pidmap = {
		[ 0 ... PIDMAP_ENTRIES-1] = { ATOMIC_INIT(BITS_PER_PAGE), NULL }
	},
	.last_pid = 0,
	.nr_hashed = PIDNS_HASH_ADDING,
	.level = 0,
	.child_reaper = &init_task,
	.user_ns = &init_user_ns,
	.ns.inum = PROC_PID_INIT_INO,
#ifdef CONFIG_PID_NS
	.ns.ops = &pidns_operations,
#endif
};
EXPORT_SYMBOL_GPL(init_pid_ns);

/*
 * Note: disable interrupts while the pidmap_lock is held as an
 * interrupt might come in and do read_lock(&tasklist_lock).
 *
 * If we don't disable interrupts there is a nasty deadlock between
 * detach_pid()->free_pid() and another cpu that does
 * spin_lock(&pidmap_lock) followed by an interrupt routine that does
 * read_lock(&tasklist_lock);
 *
 * After we clean up the tasklist_lock and know there are no
 * irq handlers that take it we can leave the interrupts enabled.
 * For now it is easier to be safe than to prove it can't happen.
 */

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

/*********************************************************************************************************
** 函数名称: free_pidmap
** 功能描述: 释放指定的 upid 到系统中 
** 输	 入: upid - 指定的 upid 指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void free_pidmap(struct upid *upid)
{
	int nr = upid->nr;
	struct pidmap *map = upid->ns->pidmap + nr / BITS_PER_PAGE;
	int offset = nr & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

/*
 * If we started walking pids at 'base', is 'a' seen before 'b'?
 */
/*********************************************************************************************************
** 函数名称: pid_before
** 功能描述: 判断指定的 pid a 是否在指定的 pid b 前 
** 输	 入: base - 指定的 pid base
**         : a - 指定的 pid a
**         : b - 指定的 pid b
** 输	 出: 1 - 在前面
**         : 0 - 不在前面
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int pid_before(int base, int a, int b)
{
	/*
	 * This is the same as saying
	 *
	 * (a - base + MAXUINT) % MAXUINT < (b - base + MAXUINT) % MAXUINT
	 * and that mapping orders 'a' and 'b' with respect to 'base'.
	 */
	return (unsigned)(a - base) < (unsigned)(b - base);
}

/*
 * We might be racing with someone else trying to set pid_ns->last_pid
 * at the pid allocation time (there's also a sysctl for this, but racing
 * with this one is OK, see comment in kernel/pid_namespace.c about it).
 * We want the winner to have the "later" value, because if the
 * "earlier" value prevails, then a pid may get reused immediately.
 *
 * Since pids rollover, it is not sufficient to just pick the bigger
 * value.  We have to consider where we started counting from.
 *
 * 'base' is the value of pid_ns->last_pid that we observed when
 * we started looking for a pid.
 *
 * 'pid' is the pid that we eventually found.
 */
static void set_last_pid(struct pid_namespace *pid_ns, int base, int pid)
{
	int prev;
	int last_write = base;
	do {
		prev = last_write;
		last_write = cmpxchg(&pid_ns->last_pid, prev, pid);
	} while ((prev != last_write) && (pid_before(base, last_write, pid)));
}

/*********************************************************************************************************
** 函数名称: alloc_pidmap
** 功能描述: 从指定的 pid namespace 中申请一个空闲的 pid 并更新 pid_ns->last_pid 的值
** 输	 入: pid_ns - 指定的 pid namespace 结构指针
** 输	 出: int - 成功申请的 pid 数值
**         : -1 - 申请失败
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static int alloc_pidmap(struct pid_namespace *pid_ns)
{
	int i, offset, max_scan, pid, last = pid_ns->last_pid;
	struct pidmap *map;

	pid = last + 1;
	if (pid >= pid_max)
		pid = RESERVED_PIDS;
	offset = pid & BITS_PER_PAGE_MASK;
	map = &pid_ns->pidmap[pid/BITS_PER_PAGE];
	/*
	 * If last_pid points into the middle of the map->page we
	 * want to scan this bitmap block twice, the second time
	 * we start with offset == 0 (or RESERVED_PIDS).
	 */
	max_scan = DIV_ROUND_UP(pid_max, BITS_PER_PAGE) - !offset;
	for (i = 0; i <= max_scan; ++i) {
		if (unlikely(!map->page)) {
			void *page = kzalloc(PAGE_SIZE, GFP_KERNEL);
			/*
			 * Free the page if someone raced with us
			 * installing it:
			 */
			spin_lock_irq(&pidmap_lock);
			if (!map->page) {
				map->page = page;
				page = NULL;
			}
			spin_unlock_irq(&pidmap_lock);
			kfree(page);
			if (unlikely(!map->page))
				break;
		}
		if (likely(atomic_read(&map->nr_free))) {
			for ( ; ; ) {
				if (!test_and_set_bit(offset, map->page)) {
					atomic_dec(&map->nr_free);
					set_last_pid(pid_ns, last, pid);
					return pid;
				}
				offset = find_next_offset(map, offset);
				if (offset >= BITS_PER_PAGE)
					break;
				pid = mk_pid(pid_ns, map, offset);
				if (pid >= pid_max)
					break;
			}
		}
		if (map < &pid_ns->pidmap[(pid_max-1)/BITS_PER_PAGE]) {
			++map;
			offset = 0;
		} else {
			map = &pid_ns->pidmap[0];
			offset = RESERVED_PIDS;
			if (unlikely(last == offset))
				break;
		}
		pid = mk_pid(pid_ns, map, offset);
	}
	return -1;
}

/*********************************************************************************************************
** 函数名称: next_pidmap
** 功能描述: 从指定的 pid namespace 的指定偏移位处开始查找下一个最接近的已经被申请的 pid 的偏移量
** 输	 入: pid_ns - 指定的 namespace 指针
**         : last - 指定的最后一个 pid 偏移位
** 输	 出: int - 和指定的 pid 最接近的 pid 的值
**         : -1 - 没找到指定的 pid 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
int next_pidmap(struct pid_namespace *pid_ns, unsigned int last)
{
	int offset;
	struct pidmap *map, *end;

	if (last >= PID_MAX_LIMIT)
		return -1;

	offset = (last + 1) & BITS_PER_PAGE_MASK;
	map = &pid_ns->pidmap[(last + 1)/BITS_PER_PAGE];
	end = &pid_ns->pidmap[PIDMAP_ENTRIES];
	for (; map < end; map++, offset = 0) {
		if (unlikely(!map->page))
			continue;
		offset = find_next_bit((map)->page, BITS_PER_PAGE, offset);
		if (offset < BITS_PER_PAGE)
			return mk_pid(pid_ns, map, offset);
	}
	return -1;
}

/*********************************************************************************************************
** 函数名称: put_pid
** 功能描述: 递减指定的 pid 的引用计数并尝试释放这个 pid 占用的资源
** 输	 入: pid - 指定的 pid 指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void put_pid(struct pid *pid)
{
	struct pid_namespace *ns;

	if (!pid)
		return;

	ns = pid->numbers[pid->level].ns;
	if ((atomic_read(&pid->count) == 1) ||
	     atomic_dec_and_test(&pid->count)) {
		kmem_cache_free(ns->pid_cachep, pid);
		put_pid_ns(ns);
	}
}
EXPORT_SYMBOL_GPL(put_pid);

/*********************************************************************************************************
** 函数名称: delayed_put_pid
** 功能描述: 
** 输	 入: pid_ns - 指定的 namespace 指针
**         : last - 指定的最后一个 pid 偏移位
** 输	 出: int - 和指定的 pid 最接近的 pid 的值
**         : -1 - 没找到指定的 pid 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void delayed_put_pid(struct rcu_head *rhp)
{
	struct pid *pid = container_of(rhp, struct pid, rcu);
	put_pid(pid);
}

/*********************************************************************************************************
** 函数名称: free_pid
** 功能描述: 释放指定的 pid 到系统中
** 输	 入: pid - 指定的 pid 指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void free_pid(struct pid *pid)
{
	/* We can be called with write_lock_irq(&tasklist_lock) held */
	int i;
	unsigned long flags;

	spin_lock_irqsave(&pidmap_lock, flags);
	for (i = 0; i <= pid->level; i++) {
		struct upid *upid = pid->numbers + i;
		struct pid_namespace *ns = upid->ns;
		hlist_del_rcu(&upid->pid_chain);
		switch(--ns->nr_hashed) {
		case 2:
		case 1:
			/* When all that is left in the pid namespace
			 * is the reaper wake up the reaper.  The reaper
			 * may be sleeping in zap_pid_ns_processes().
			 */
			wake_up_process(ns->child_reaper);
			break;
		case PIDNS_HASH_ADDING:
			/* Handle a fork failure of the first process */
			WARN_ON(ns->child_reaper);
			ns->nr_hashed = 0;
			/* fall through */
		case 0:
			schedule_work(&ns->proc_work);
			break;
		}
	}
	spin_unlock_irqrestore(&pidmap_lock, flags);

	for (i = 0; i <= pid->level; i++)
		free_pidmap(pid->numbers + i);

	call_rcu(&pid->rcu, delayed_put_pid);
}

/*********************************************************************************************************
** 函数名称: alloc_pid
** 功能描述: 从指定的 pid namespace 中申请一个 pid
** 输	 入: ns - 指定的 pid namespace 指针
** 输	 出: pid * - 成功申请的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid *alloc_pid(struct pid_namespace *ns)
{
	struct pid *pid;
	enum pid_type type;
	int i, nr;
	struct pid_namespace *tmp;
	struct upid *upid;

	pid = kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);
	if (!pid)
		goto out;

	tmp = ns;
	pid->level = ns->level;
	for (i = ns->level; i >= 0; i--) {
		nr = alloc_pidmap(tmp);
		if (nr < 0)
			goto out_free;

		pid->numbers[i].nr = nr;
		pid->numbers[i].ns = tmp;
		tmp = tmp->parent;
	}

	if (unlikely(is_child_reaper(pid))) {
		if (pid_ns_prepare_proc(ns))
			goto out_free;
	}

	get_pid_ns(ns);
	atomic_set(&pid->count, 1);
	for (type = 0; type < PIDTYPE_MAX; ++type)
		INIT_HLIST_HEAD(&pid->tasks[type]);

	upid = pid->numbers + ns->level;
	spin_lock_irq(&pidmap_lock);
	if (!(ns->nr_hashed & PIDNS_HASH_ADDING))
		goto out_unlock;
	for ( ; upid >= pid->numbers; --upid) {
		hlist_add_head_rcu(&upid->pid_chain,
				&pid_hash[pid_hashfn(upid->nr, upid->ns)]);
		upid->ns->nr_hashed++;
	}
	spin_unlock_irq(&pidmap_lock);

out:
	return pid;

out_unlock:
	spin_unlock_irq(&pidmap_lock);
	put_pid_ns(ns);

out_free:
	while (++i <= ns->level)
		free_pidmap(pid->numbers + i);

	kmem_cache_free(ns->pid_cachep, pid);
	pid = NULL;
	goto out;
}

void disable_pid_allocation(struct pid_namespace *ns)
{
	spin_lock_irq(&pidmap_lock);
	ns->nr_hashed &= ~PIDNS_HASH_ADDING;
	spin_unlock_irq(&pidmap_lock);
}

/*********************************************************************************************************
** 函数名称: find_pid_ns
** 功能描述: 通过指定的 pid 偏移量和指定的 pid namespace 查找与其匹配的 pid 结构体指针
** 输	 入: nr - 指定的 pid 偏移量
**         : ns - 指定的 pid namespace 指针
** 输	 出: pid * - 查找到匹配的 pid 结构指针
**         : NULL - 没找到匹配的 pid 结构
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid *find_pid_ns(int nr, struct pid_namespace *ns)
{
	struct upid *pnr;

    /* 遍历 pid 散列哈希值相同的 pid 链表 */
	hlist_for_each_entry_rcu(pnr,
			&pid_hash[pid_hashfn(nr, ns)], pid_chain)
		if (pnr->nr == nr && pnr->ns == ns)
			return container_of(pnr, struct pid,
					numbers[ns->level]);

	return NULL;
}
EXPORT_SYMBOL_GPL(find_pid_ns);

/*********************************************************************************************************
** 函数名称: find_vpid
** 功能描述: 当前系统正在运行的任务的 pid namespace 中查找和指定的 pid 偏移量对应的 pid 结构指针
** 输	 入: nr - 指定的 pid 偏移量
** 输	 出: pid * - 当前运行任务的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid *find_vpid(int nr)
{
	return find_pid_ns(nr, task_active_pid_ns(current));
}
EXPORT_SYMBOL_GPL(find_vpid);

/*
 * attach_pid() must be called with the tasklist_lock write-held.
 */
/*********************************************************************************************************
** 函数名称: attach_pid
** 功能描述: 把指定的任务添加到指定类型的 pid 的任务链表中
** 输	 入: task - 指定的任务指针
**         : type - 指定的 pid 类型
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void attach_pid(struct task_struct *task, enum pid_type type)
{
	struct pid_link *link = &task->pids[type];
	hlist_add_head_rcu(&link->node, &link->pid->tasks[type]);
}

static void __change_pid(struct task_struct *task, enum pid_type type,
			struct pid *new)
{
	struct pid_link *link;
	struct pid *pid;
	int tmp;

	link = &task->pids[type];
	pid = link->pid;

	hlist_del_rcu(&link->node);
	link->pid = new;

	for (tmp = PIDTYPE_MAX; --tmp >= 0; )
		if (!hlist_empty(&pid->tasks[tmp]))
			return;

	free_pid(pid);
}

void detach_pid(struct task_struct *task, enum pid_type type)
{
	__change_pid(task, type, NULL);
}

void change_pid(struct task_struct *task, enum pid_type type,
		struct pid *pid)
{
	__change_pid(task, type, pid);
	attach_pid(task, type);
}

/* transfer_pid is an optimization of attach_pid(new), detach_pid(old) */
void transfer_pid(struct task_struct *old, struct task_struct *new,
			   enum pid_type type)
{
	new->pids[type].pid = old->pids[type].pid;
	hlist_replace_rcu(&old->pids[type].node, &new->pids[type].node);
}

/*********************************************************************************************************
** 函数名称: pid_task
** 功能描述: 通过指定类型的 pid 获取使用这个 pid 的第一个任务结构指针
** 输	 入: pid - 指定的 pid 结构指针
**         : type - 指定的 pid 类型
** 输	 出: result - 使用指定 pid 的第一个任务结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct task_struct *pid_task(struct pid *pid, enum pid_type type)
{
	struct task_struct *result = NULL;
	if (pid) {
		struct hlist_node *first;
		first = rcu_dereference_check(hlist_first_rcu(&pid->tasks[type]),
					      lockdep_tasklist_lock_is_held());
		if (first)
			result = hlist_entry(first, struct task_struct, pids[(type)].node);
	}
	return result;
}
EXPORT_SYMBOL(pid_task);

/*
 * Must be called under rcu_read_lock().
 */
/*********************************************************************************************************
** 函数名称: find_task_by_pid_ns
** 功能描述: 通过指定的 pid 偏移量和指定的 pid namespace 查找使用这个 pid 的第一个任务结构指针
** 输	 入: nr - 指定的 pid 偏移量
**         : ns - 指定的 pid namespace
** 输	 出: result - 使用指定 pid 的第一个任务结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct task_struct *find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns)
{
	rcu_lockdep_assert(rcu_read_lock_held(),
			   "find_task_by_pid_ns() needs rcu_read_lock()"
			   " protection");
	return pid_task(find_pid_ns(nr, ns), PIDTYPE_PID);
}

/*********************************************************************************************************
** 函数名称: find_task_by_vpid
** 功能描述: 通过指定的 pid 偏移量查找在当前正在运行的进程的 namespace 中查找对应的任务结构指针
** 输	 入: vnr - 指定的 pid 偏移量
** 输	 出: task_struct * - 匹配的任务结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct task_struct *find_task_by_vpid(pid_t vnr)
{
	return find_task_by_pid_ns(vnr, task_active_pid_ns(current));
}

/*********************************************************************************************************
** 函数名称: get_task_pid
** 功能描述: 获取指定任务的（属于某个进程）指定类型的 pid 数值
** 输	 入: task - 指定的任务指针
**         : type - 指定的 pid 类型
** 输	 出: pid - 成功获取的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid *get_task_pid(struct task_struct *task, enum pid_type type)
{
	struct pid *pid;
	rcu_read_lock();
	if (type != PIDTYPE_PID)
		task = task->group_leader;
	pid = get_pid(task->pids[type].pid);
	rcu_read_unlock();
	return pid;
}
EXPORT_SYMBOL_GPL(get_task_pid);

/*********************************************************************************************************
** 函数名称: get_pid_task
** 功能描述: 通过指定类型的 pid 查找与其对应的任务结构指针
** 输	 入: pid - 指定的 pid 结构指针
**         : type - 指定的 pid 类型
** 输	 出: result - 匹配的任务结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct task_struct *get_pid_task(struct pid *pid, enum pid_type type)
{
	struct task_struct *result;
	rcu_read_lock();
	result = pid_task(pid, type);
	if (result)
		get_task_struct(result);
	rcu_read_unlock();
	return result;
}
EXPORT_SYMBOL_GPL(get_pid_task);

/*********************************************************************************************************
** 函数名称: find_vpid
** 功能描述: 当前系统正在运行的任务的 pid namespace 中查找和指定的 pid 偏移量对应的 pid 结构指针
** 输	 入: nr - 指定的 pid 偏移量
** 输	 出: pid * - 当前运行任务的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid *find_get_pid(pid_t nr)
{
	struct pid *pid;

	rcu_read_lock();
	pid = get_pid(find_vpid(nr));
	rcu_read_unlock();

	return pid;
}
EXPORT_SYMBOL_GPL(find_get_pid);

/*********************************************************************************************************
** 函数名称: pid_nr_ns
** 功能描述: 获取指定的 pid 结构指针在指定的 pid namespace 中的 pid 偏移量
** 输	 入: pid - 指定的 pid 结构指针
**         : ns - 指定的 pid namespace 结构指针
** 输	 出: nr - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
{
	struct upid *upid;
	pid_t nr = 0;

	if (pid && ns->level <= pid->level) {
		upid = &pid->numbers[ns->level];
		if (upid->ns == ns)
			nr = upid->nr;
	}
	return nr;
}
EXPORT_SYMBOL_GPL(pid_nr_ns);

/*********************************************************************************************************
** 函数名称: pid_vnr
** 功能描述: 通过指定的 pid 结构指针和当前正在运行的进程的 pid namespace 获取与其对应的 pid 偏移量
** 输	 入: pid - 指定的 pid 结构指针
** 输	 出: nr - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
pid_t pid_vnr(struct pid *pid)
{
	return pid_nr_ns(pid, task_active_pid_ns(current));
}
EXPORT_SYMBOL_GPL(pid_vnr);

/*********************************************************************************************************
** 函数名称: __task_pid_nr_ns
** 功能描述: 获取指定任务的指定类型的 pid 在指定的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
**         : type - 指定的 pid 类型
**         : ns - 指定的 pid namespace 结构指针
** 输	 出: nr - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type,
			struct pid_namespace *ns)
{
	pid_t nr = 0;

	rcu_read_lock();
	if (!ns)
		ns = task_active_pid_ns(current);
	if (likely(pid_alive(task))) {
		if (type != PIDTYPE_PID)
			task = task->group_leader;
		nr = pid_nr_ns(task->pids[type].pid, ns);
	}
	rcu_read_unlock();

	return nr;
}
EXPORT_SYMBOL(__task_pid_nr_ns);

/*********************************************************************************************************
** 函数名称: task_tgid_nr_ns
** 功能描述: 获取指定任务的进程组组长在指定的 pid namespace 中的 pid 偏移量 
** 输	 入: task - 指定的任务结构指针
**         : ns - 指定的 pid namespace 结构指针
** 输	 出: nr - 获取到的对应的 pid 偏移量
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
pid_t task_tgid_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return pid_nr_ns(task_tgid(tsk), ns);
}
EXPORT_SYMBOL(task_tgid_nr_ns);

/*********************************************************************************************************
** 函数名称: task_active_pid_ns
** 功能描述: 获取指定任务的进程 pid 所在的 pid namespace 结构指针
** 输	 入: task - 指定的任务指针
** 输	 出: pid_namespace * - 获取到的 pid namespace 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid_namespace *task_active_pid_ns(struct task_struct *tsk)
{
	return ns_of_pid(task_pid(tsk));
}
EXPORT_SYMBOL_GPL(task_active_pid_ns);

/*
 * Used by proc to find the first pid that is greater than or equal to nr.
 *
 * If there is a pid at nr this function is exactly the same as find_pid_ns.
 */
/*********************************************************************************************************
** 函数名称: find_ge_pid
** 功能描述: 在指定的 pid namespace 中获取最接近且大于等于指定的 pid 偏移量的 pid 结构指针 
** 输	 入: nr - 指定的 pid 偏移量
**         : ns - 指定的 pid namespace 结构指针
** 输	 出: nr - 获取到的匹配的 pid 结构指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
struct pid *find_ge_pid(int nr, struct pid_namespace *ns)
{
	struct pid *pid;

	do {
		pid = find_pid_ns(nr, ns);
		if (pid)
			break;
		nr = next_pidmap(ns, nr);
	} while (nr > 0);

	return pid;
}

/*
 * The pid hash table is scaled according to the amount of memory in the
 * machine.  From a minimum of 16 slots up to 4096 slots at one gigabyte or
 * more.
 */
/*********************************************************************************************************
** 函数名称: pidhash_init
** 功能描述: 初始化 pid 模块使用的 pid 散列哈希数组结构 
** 输	 入: 
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __init pidhash_init(void)
{
	unsigned int i, pidhash_size;

	pid_hash = alloc_large_system_hash("PID", sizeof(*pid_hash), 0, 18,
					   HASH_EARLY | HASH_SMALL,
					   &pidhash_shift, NULL,
					   0, 4096);
	pidhash_size = 1U << pidhash_shift;

	for (i = 0; i < pidhash_size; i++)
		INIT_HLIST_HEAD(&pid_hash[i]);
}

/*********************************************************************************************************
** 函数名称: pidmap_init
** 功能描述: 初始化 pid 模块使用的 pid 散列哈希数组结构 
** 输	 入: 初始化 pid 模块使用的 pidmap 变量值以及系统 0 号进程的 pidmap 数据结构
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void __init pidmap_init(void)
{
	/* Veryify no one has done anything silly */
	BUILD_BUG_ON(PID_MAX_LIMIT >= PIDNS_HASH_ADDING);

	/* bump default and minimum pid_max based on number of cpus */
	pid_max = min(pid_max_max, max_t(int, pid_max,
				PIDS_PER_CPU_DEFAULT * num_possible_cpus()));
	pid_max_min = max_t(int, pid_max_min,
				PIDS_PER_CPU_MIN * num_possible_cpus());
	pr_info("pid_max: default: %u minimum: %u\n", pid_max, pid_max_min);

	init_pid_ns.pidmap[0].page = kzalloc(PAGE_SIZE, GFP_KERNEL);
	/* Reserve PID 0. We never call free_pidmap(0) */
	set_bit(0, init_pid_ns.pidmap[0].page);
	atomic_dec(&init_pid_ns.pidmap[0].nr_free);

	init_pid_ns.pid_cachep = KMEM_CACHE(pid,
			SLAB_HWCACHE_ALIGN | SLAB_PANIC);
}
