/*
 * Copyright (C) 2014 Davidlohr Bueso.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmacache.h>

/*
 * Flush vma caches for threads that share a given mm.
 *
 * The operation is safe because the caller holds the mmap_sem
 * exclusively and other threads accessing the vma cache will
 * have mmap_sem held at least for read, so no extra locking
 * is required to maintain the vma cache.
 */
void vmacache_flush_all(struct mm_struct *mm)
{
	struct task_struct *g, *p;

	count_vm_vmacache_event(VMACACHE_FULL_FLUSHES);

	/*
	 * Single threaded tasks need not iterate the entire
	 * list of process. We can avoid the flushing as well
	 * since the mm's seqnum was increased and don't have
	 * to worry about other threads' seqnum. Current's
	 * flush will occur upon the next lookup.
	 */
	if (atomic_read(&mm->mm_users) == 1)
		return;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		/*
		 * Only flush the vmacache pointers as the
		 * mm seqnum is already set and curr's will
		 * be set upon invalidation when the next
		 * lookup is done.
		 */
		if (mm == p->mm)
			vmacache_flush(p);
	}
	rcu_read_unlock();
}

/*
 * This task may be accessing a foreign mm via (for example)
 * get_user_pages()->find_vma().  The vmacache is task-local and this
 * task's vmacache pertains to a different mm (ie, its own).  There is
 * nothing we can do here.
 *
 * Also handle the case where a kernel thread has adopted this mm via use_mm().
 * That kernel thread's vmacache is not applicable to this mm.
 */
// 确保指定的进程地址空间是当前正在运行进程的，并且当前进程不能是内核进程
// 因为内核线程没有进程地址空间，所以常规情况下不可以操作进程地址
static bool vmacache_valid_mm(struct mm_struct *mm)
{
	return current->mm == mm && !(current->flags & PF_KTHREAD);
}

// 把指定的 vma 数据结构缓存到当前进程的 vma cache 数组中
// 因为大部分程序在执行时都具有局部性特性，所以下一次想要查找地址很大概率在
// 这次查找的地址附近，所以我们把这次找到的 vma 数据结构缓存下来，这样会提高
// 整个系统的地址查找效率 
void vmacache_update(unsigned long addr, struct vm_area_struct *newvma)
{
	if (vmacache_valid_mm(newvma->vm_mm))
		current->vmacache[VMACACHE_HASH(addr)] = newvma;
}

// 判断指定的进程地址空间的 vma cache 数据是否有效
static bool vmacache_valid(struct mm_struct *mm)
{
	struct task_struct *curr;

	// 判断指定的进程地址空间对当前进程是否有效
	if (!vmacache_valid_mm(mm))
		return false;

	// 判断当前进程的 vmacache_seqnum 是否有效
	curr = current;
	if (mm->vmacache_seqnum != curr->vmacache_seqnum) {
		/*
		 * First attempt will always be invalid, initialize
		 * the new cache for this task here.
		 */
		curr->vmacache_seqnum = mm->vmacache_seqnum;
		vmacache_flush(curr);
		return false;
	}
	return true;
}

// 分别遍历当前进程的 vma cache 成员，查找包含我们指定的地址的
// vma，如果找到这样的 vma，则直接返回 vma 结构的地址
struct vm_area_struct *vmacache_find(struct mm_struct *mm, unsigned long addr)
{
	int i;

	// 判断指定的进程地址空间的 vma cache 数据是否有效
	if (!vmacache_valid(mm))
		return NULL;

	count_vm_vmacache_event(VMACACHE_FIND_CALLS);

	// 分别遍历当前进程的 vma cache 成员，查找包含我们指定的地址的
	// vma，如果找到这样的 vma，则直接返回 vma 结构的地址
	for (i = 0; i < VMACACHE_SIZE; i++) {
		struct vm_area_struct *vma = current->vmacache[i];

		if (!vma)
			continue;
		if (WARN_ON_ONCE(vma->vm_mm != mm))
			break;

		// 如果我们想要查找的虚拟地址在这个 vma 地址块空间范围内
		// 则直接返回这个 vma 数据结构地址
		if (vma->vm_start <= addr && vma->vm_end > addr) {
			count_vm_vmacache_event(VMACACHE_FIND_HITS);
			return vma;
		}
	}

	return NULL;
}

#ifndef CONFIG_MMU
struct vm_area_struct *vmacache_find_exact(struct mm_struct *mm,
					   unsigned long start,
					   unsigned long end)
{
	int i;

	if (!vmacache_valid(mm))
		return NULL;

	count_vm_vmacache_event(VMACACHE_FIND_CALLS);

	for (i = 0; i < VMACACHE_SIZE; i++) {
		struct vm_area_struct *vma = current->vmacache[i];

		if (vma && vma->vm_start == start && vma->vm_end == end) {
			count_vm_vmacache_event(VMACACHE_FIND_HITS);
			return vma;
		}
	}

	return NULL;
}
#endif
