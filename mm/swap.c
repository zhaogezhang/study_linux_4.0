/*
 *  linux/mm/swap.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

/*
 * This file contains the default values for the operation of the
 * Linux VM subsystem. Fine-tuning documentation can be found in
 * Documentation/sysctl/vm.txt.
 * Started 18.12.91
 * Swap aging added 23.2.95, Stephen Tweedie.
 * Buffermem limits added 12.3.98, Rik van Riel.
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/mm_inline.h>
#include <linux/percpu_counter.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/backing-dev.h>
#include <linux/memcontrol.h>
#include <linux/gfp.h>
#include <linux/uio.h>

#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/pagemap.h>

/* How many pages do we try to swap or page in/out together? */
int page_cluster;

static DEFINE_PER_CPU(struct pagevec, lru_add_pvec);
static DEFINE_PER_CPU(struct pagevec, lru_rotate_pvecs);
static DEFINE_PER_CPU(struct pagevec, lru_deactivate_pvecs);

/*
 * This path almost never happens for VM activity - pages are normally
 * freed via pagevecs.  But it gets used by networking.
 */
static void __page_cache_release(struct page *page)
{
	if (PageLRU(page)) {
		struct zone *zone = page_zone(page);
		struct lruvec *lruvec;
		unsigned long flags;

		spin_lock_irqsave(&zone->lru_lock, flags);
		lruvec = mem_cgroup_page_lruvec(page, zone);
		VM_BUG_ON_PAGE(!PageLRU(page), page);
		__ClearPageLRU(page);
		del_page_from_lru_list(page, lruvec, page_off_lru(page));
		spin_unlock_irqrestore(&zone->lru_lock, flags);
	}
	mem_cgroup_uncharge(page);
}

static void __put_single_page(struct page *page)
{
	__page_cache_release(page);
	free_hot_cold_page(page, false);
}

static void __put_compound_page(struct page *page)
{
	compound_page_dtor *dtor;

	__page_cache_release(page);
	dtor = get_compound_page_dtor(page);
	(*dtor)(page);
}

/**
 * Two special cases here: we could avoid taking compound_lock_irqsave
 * and could skip the tail refcounting(in _mapcount).
 *
 * 1. Hugetlbfs page:
 *
 *    PageHeadHuge will remain true until the compound page
 *    is released and enters the buddy allocator, and it could
 *    not be split by __split_huge_page_refcount().
 *
 *    So if we see PageHeadHuge set, and we have the tail page pin,
 *    then we could safely put head page.
 *
 * 2. Slab THP page:
 *
 *    PG_slab is cleared before the slab frees the head page, and
 *    tail pin cannot be the last reference left on the head page,
 *    because the slab code is free to reuse the compound page
 *    after a kfree/kmem_cache_free without having to check if
 *    there's any tail pin left.  In turn all tail pinsmust be always
 *    released while the head is still pinned by the slab code
 *    and so we know PG_slab will be still set too.
 *
 *    So if we see PageSlab set, and we have the tail page pin,
 *    then we could safely put head page.
 */
static __always_inline
void put_unrefcounted_compound_page(struct page *page_head, struct page *page)
{
	/*
	 * If @page is a THP tail, we must read the tail page
	 * flags after the head page flags. The
	 * __split_huge_page_refcount side enforces write memory barriers
	 * between clearing PageTail and before the head page
	 * can be freed and reallocated.
	 */
	smp_rmb();
	if (likely(PageTail(page))) {
		/*
		 * __split_huge_page_refcount cannot race
		 * here, see the comment above this function.
		 */
		VM_BUG_ON_PAGE(!PageHead(page_head), page_head);
		VM_BUG_ON_PAGE(page_mapcount(page) != 0, page);
		if (put_page_testzero(page_head)) {
			/*
			 * If this is the tail of a slab THP page,
			 * the tail pin must not be the last reference
			 * held on the page, because the PG_slab cannot
			 * be cleared before all tail pins (which skips
			 * the _mapcount tail refcounting) have been
			 * released.
			 *
			 * If this is the tail of a hugetlbfs page,
			 * the tail pin may be the last reference on
			 * the page instead, because PageHeadHuge will
			 * not go away until the compound page enters
			 * the buddy allocator.
			 */
			VM_BUG_ON_PAGE(PageSlab(page_head), page_head);
			__put_compound_page(page_head);
		}
	} else
		/*
		 * __split_huge_page_refcount run before us,
		 * @page was a THP tail. The split @page_head
		 * has been freed and reallocated as slab or
		 * hugetlbfs page of smaller order (only
		 * possible if reallocated as slab on x86).
		 */
		if (put_page_testzero(page))
			__put_single_page(page);
}

static __always_inline
void put_refcounted_compound_page(struct page *page_head, struct page *page)
{
	if (likely(page != page_head && get_page_unless_zero(page_head))) {
		unsigned long flags;

		/*
		 * @page_head wasn't a dangling pointer but it may not
		 * be a head page anymore by the time we obtain the
		 * lock. That is ok as long as it can't be freed from
		 * under us.
		 */
		flags = compound_lock_irqsave(page_head);
		if (unlikely(!PageTail(page))) {
			/* __split_huge_page_refcount run before us */
			compound_unlock_irqrestore(page_head, flags);
			if (put_page_testzero(page_head)) {
				/*
				 * The @page_head may have been freed
				 * and reallocated as a compound page
				 * of smaller order and then freed
				 * again.  All we know is that it
				 * cannot have become: a THP page, a
				 * compound page of higher order, a
				 * tail page.  That is because we
				 * still hold the refcount of the
				 * split THP tail and page_head was
				 * the THP head before the split.
				 */
				if (PageHead(page_head))
					__put_compound_page(page_head);
				else
					__put_single_page(page_head);
			}
out_put_single:
			if (put_page_testzero(page))
				__put_single_page(page);
			return;
		}
		VM_BUG_ON_PAGE(page_head != page->first_page, page);
		/*
		 * We can release the refcount taken by
		 * get_page_unless_zero() now that
		 * __split_huge_page_refcount() is blocked on the
		 * compound_lock.
		 */
		if (put_page_testzero(page_head))
			VM_BUG_ON_PAGE(1, page_head);
		/* __split_huge_page_refcount will wait now */
		VM_BUG_ON_PAGE(page_mapcount(page) <= 0, page);
		atomic_dec(&page->_mapcount);
		VM_BUG_ON_PAGE(atomic_read(&page_head->_count) <= 0, page_head);
		VM_BUG_ON_PAGE(atomic_read(&page->_count) != 0, page);
		compound_unlock_irqrestore(page_head, flags);

		if (put_page_testzero(page_head)) {
			if (PageHead(page_head))
				__put_compound_page(page_head);
			else
				__put_single_page(page_head);
		}
	} else {
		/* @page_head is a dangling pointer */
		VM_BUG_ON_PAGE(PageTail(page), page);
		goto out_put_single;
	}
}

static void put_compound_page(struct page *page)
{
	struct page *page_head;

	/*
	 * We see the PageCompound set and PageTail not set, so @page maybe:
	 *  1. hugetlbfs head page, or
	 *  2. THP head page.
	 */
	if (likely(!PageTail(page))) {
		if (put_page_testzero(page)) {
			/*
			 * By the time all refcounts have been released
			 * split_huge_page cannot run anymore from under us.
			 */
			if (PageHead(page))
				__put_compound_page(page);
			else
				__put_single_page(page);
		}
		return;
	}

	/*
	 * We see the PageCompound set and PageTail set, so @page maybe:
	 *  1. a tail hugetlbfs page, or
	 *  2. a tail THP page, or
	 *  3. a split THP page.
	 *
	 *  Case 3 is possible, as we may race with
	 *  __split_huge_page_refcount tearing down a THP page.
	 */
	page_head = compound_head_by_tail(page);
	if (!__compound_tail_refcounted(page_head))
		put_unrefcounted_compound_page(page_head, page);
	else
		put_refcounted_compound_page(page_head, page);
}

void put_page(struct page *page)
{
	if (unlikely(PageCompound(page)))
		put_compound_page(page);
	else if (put_page_testzero(page))
		__put_single_page(page);
}
EXPORT_SYMBOL(put_page);

/*
 * This function is exported but must not be called by anything other
 * than get_page(). It implements the slow path of get_page().
 */
bool __get_page_tail(struct page *page)
{
	/*
	 * This takes care of get_page() if run on a tail page
	 * returned by one of the get_user_pages/follow_page variants.
	 * get_user_pages/follow_page itself doesn't need the compound
	 * lock because it runs __get_page_tail_foll() under the
	 * proper PT lock that already serializes against
	 * split_huge_page().
	 */
	unsigned long flags;
	bool got;
	struct page *page_head = compound_head(page);

	/* Ref to put_compound_page() comment. */
	if (!__compound_tail_refcounted(page_head)) {
		smp_rmb();
		if (likely(PageTail(page))) {
			/*
			 * This is a hugetlbfs page or a slab
			 * page. __split_huge_page_refcount
			 * cannot race here.
			 */
			VM_BUG_ON_PAGE(!PageHead(page_head), page_head);
			__get_page_tail_foll(page, true);
			return true;
		} else {
			/*
			 * __split_huge_page_refcount run
			 * before us, "page" was a THP
			 * tail. The split page_head has been
			 * freed and reallocated as slab or
			 * hugetlbfs page of smaller order
			 * (only possible if reallocated as
			 * slab on x86).
			 */
			return false;
		}
	}

	got = false;
	if (likely(page != page_head && get_page_unless_zero(page_head))) {
		/*
		 * page_head wasn't a dangling pointer but it
		 * may not be a head page anymore by the time
		 * we obtain the lock. That is ok as long as it
		 * can't be freed from under us.
		 */
		flags = compound_lock_irqsave(page_head);
		/* here __split_huge_page_refcount won't run anymore */
		if (likely(PageTail(page))) {
			__get_page_tail_foll(page, false);
			got = true;
		}
		compound_unlock_irqrestore(page_head, flags);
		if (unlikely(!got))
			put_page(page_head);
	}
	return got;
}
EXPORT_SYMBOL(__get_page_tail);

/**
 * put_pages_list() - release a list of pages
 * @pages: list of pages threaded on page->lru
 *
 * Release a list of pages which are strung together on page.lru.  Currently
 * used by read_cache_pages() and related error recovery code.
 */
void put_pages_list(struct list_head *pages)
{
	while (!list_empty(pages)) {
		struct page *victim;

		victim = list_entry(pages->prev, struct page, lru);
		list_del(&victim->lru);
		page_cache_release(victim);
	}
}
EXPORT_SYMBOL(put_pages_list);

/*
 * get_kernel_pages() - pin kernel pages in memory
 * @kiov:	An array of struct kvec structures
 * @nr_segs:	number of segments to pin
 * @write:	pinning for read/write, currently ignored
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_segs long.
 *
 * Returns number of pages pinned. This may be fewer than the number
 * requested. If nr_pages is 0 or negative, returns 0. If no pages
 * were pinned, returns -errno. Each page returned must be released
 * with a put_page() call when it is finished with.
 */
int get_kernel_pages(const struct kvec *kiov, int nr_segs, int write,
		struct page **pages)
{
	int seg;

	for (seg = 0; seg < nr_segs; seg++) {
		if (WARN_ON(kiov[seg].iov_len != PAGE_SIZE))
			return seg;

		pages[seg] = kmap_to_page(kiov[seg].iov_base);
		page_cache_get(pages[seg]);
	}

	return seg;
}
EXPORT_SYMBOL_GPL(get_kernel_pages);

/*
 * get_kernel_page() - pin a kernel page in memory
 * @start:	starting kernel address
 * @write:	pinning for read/write, currently ignored
 * @pages:	array that receives pointer to the page pinned.
 *		Must be at least nr_segs long.
 *
 * Returns 1 if page is pinned. If the page was not pinned, returns
 * -errno. The page returned must be released with a put_page() call
 * when it is finished with.
 */
int get_kernel_page(unsigned long start, int write, struct page **pages)
{
	const struct kvec kiov = {
		.iov_base = (void *)start,
		.iov_len = PAGE_SIZE
	};

	return get_kernel_pages(&kiov, 1, write, pages);
}
EXPORT_SYMBOL_GPL(get_kernel_page);

// 遍历 pagevec，并对每一个内存页执行 move_fn 函数
static void pagevec_lru_move_fn(struct pagevec *pvec,
	void (*move_fn)(struct page *page, struct lruvec *lruvec, void *arg),
	void *arg)
{
	int i;
	struct zone *zone = NULL;
	struct lruvec *lruvec;
	unsigned long flags = 0;

	// 分别遍历 lru 缓存（pagevec）中的每一个内存页，并执行 move_fn 函数
	for (i = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		struct zone *pagezone = page_zone(page);

		// 由于不同页可能加入到的 zone 不同，这样就是判断是否是同一个 zone，是的话就不需要上锁了
        // 不是的话要先把之前上锁的 zone 解锁，再对此 zone 的 lru_lock 上锁
		if (pagezone != zone) {
			if (zone)
				spin_unlock_irqrestore(&zone->lru_lock, flags);
			zone = pagezone;
			spin_lock_irqsave(&zone->lru_lock, flags);
		}

		lruvec = mem_cgroup_page_lruvec(page, zone);
		(*move_fn)(page, lruvec, arg);
	}
	if (zone)
		spin_unlock_irqrestore(&zone->lru_lock, flags);
	release_pages(pvec->pages, pvec->nr, pvec->cold);
	pagevec_reinit(pvec);
}

// 如果指定的内存页是 inactive（匿名页或文件页）状态的，则把这个页移动到对应的 inactive 链表的末尾位置
// 1. 这个函数可以在把 lru active 链表上状态为 inactive 的内存页移动到与其对应的 lru inactive 链表尾部位置
// 2. 这个函数可以在把 lru_rotate_pvecs 缓存上状态为 inactive 的内存页移动到与其对应的 lru inactive 链表尾部位置
// 3. ...
static void pagevec_move_tail_fn(struct page *page, struct lruvec *lruvec,
				 void *arg)
{
	int *pgmoved = arg;

	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
		enum lru_list lru = page_lru_base_type(page);
		list_move_tail(&page->lru, &lruvec->lists[lru]);
		(*pgmoved)++;
	}
}

/*
 * pagevec_move_tail() must be called with IRQ disabled.
 * Otherwise this may cause nasty races.
 */
// 如果指定的内存页状态为 inactive（匿名页或文件页），则把这个页移动到与其对应的 inactive 链表的末尾位置
static void pagevec_move_tail(struct pagevec *pvec)
{
	int pgmoved = 0;

	pagevec_lru_move_fn(pvec, pagevec_move_tail_fn, &pgmoved);
	__count_vm_events(PGROTATED, pgmoved);
}

/*
 * Writeback is about to end against a page which has been marked for immediate
 * reclaim.  If it still appears to be reclaimable, move it to the tail of the
 * inactive list.
 */
// 把指定的内存页添加到 inactive lru 缓存链表中（lru_rotate_pvecs）
// 如果这个链表在加入新页后满了，则把
void rotate_reclaimable_page(struct page *page)
{
	if (!PageLocked(page) && !PageDirty(page) && !PageActive(page) &&
	    !PageUnevictable(page) && PageLRU(page)) {
		struct pagevec *pvec;
		unsigned long flags;

		// 增加页引用计数
		page_cache_get(page);
		
		local_irq_save(flags);
		
		pvec = this_cpu_ptr(&lru_rotate_pvecs);
		if (!pagevec_add(pvec, page))
			pagevec_move_tail(pvec);
		
		local_irq_restore(flags);
	}
}

// 更新 lru 链表中，scanned 链表和 rotated 链表中的成员计数
// scanned 链表：所有（匿名链表和文件链表）链表中 active 和 inactive 两个链表中成员统计数
// rotated 链表：所有（匿名链表和文件链表）链表中 active 链表中成员统计数
static void update_page_reclaim_stat(struct lruvec *lruvec,
				     int file, int rotated)
{
	struct zone_reclaim_stat *reclaim_stat = &lruvec->reclaim_stat;

	reclaim_stat->recent_scanned[file]++;
	if (rotated)
		reclaim_stat->recent_rotated[file]++;
}

// 把指定的内存页从指定的 inactive lru 链表中移除，并添加到与其对应的 active lru 链表上
// 同时更新相关的状态统计变量信息（PG_active）
static void __activate_page(struct page *page, struct lruvec *lruvec,
			    void *arg)
{
	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
		int file = page_is_file_cache(page);
		int lru = page_lru_base_type(page);

		del_page_from_lru_list(page, lruvec, lru);
		SetPageActive(page);
		lru += LRU_ACTIVE;
		add_page_to_lru_list(page, lruvec, lru);
		trace_mm_lru_activate(page);

		__count_vm_event(PGACTIVATE);
		update_page_reclaim_stat(lruvec, file, 1);
	}
}

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct pagevec, activate_page_pvecs);

// 如果本地 cpu 的 activate_page 页向量（activate_page_pvecs）不为空，那么依次
// 遍历这个链表上的每一个内存页，并将其从  inactive lru 链表中移除，并添加到与其
// 对应的 active lru 链表上
static void activate_page_drain(int cpu)
{
	struct pagevec *pvec = &per_cpu(activate_page_pvecs, cpu);

	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, __activate_page, NULL);
}

// 判断指定 cpu 上 active lru 缓存链表（activate_page_pvecs）是否为空
static bool need_activate_page_drain(int cpu)
{
	return pagevec_count(&per_cpu(activate_page_pvecs, cpu)) != 0;
}

// 把 inactive lru 链表上的内存页添加到 active lru 缓存（pagevec）中，如果 active lru
// 缓存满了，则把 active lru 缓存中的所有页添加到 lru active 链表上
// 在指定页添加到 lru 缓存上时没有立即修改 active 状态，而是在 lru 缓存满时，把这些页
// 放到 lru 链表上的时候，才设置这些页为 active 状态
void activate_page(struct page *page)
{
	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
		struct pagevec *pvec = &get_cpu_var(activate_page_pvecs);

		page_cache_get(page);
		if (!pagevec_add(pvec, page))
			pagevec_lru_move_fn(pvec, __activate_page, NULL);
		put_cpu_var(activate_page_pvecs);
	}
}

#else
static inline void activate_page_drain(int cpu)
{
}

static bool need_activate_page_drain(int cpu)
{
	return false;
}

// 把指定的内存页从指定的 inactive lru 链表中移除，并添加到与其对应的 active lru 链表上
// 同时更新相关的状态统计变量信息（PG_active）
void activate_page(struct page *page)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(&zone->lru_lock);
	__activate_page(page, mem_cgroup_page_lruvec(page, zone), NULL);
	spin_unlock_irq(&zone->lru_lock);
}
#endif

// 设置 lru_add_pvec 缓存中指定的内存页的 PG_active 标志
static void __lru_cache_activate_page(struct page *page)
{
	struct pagevec *pvec = &get_cpu_var(lru_add_pvec);
	int i;

	/*
	 * Search backwards on the optimistic assumption that the page being
	 * activated has just been added to this pagevec. Note that only
	 * the local pagevec is examined as a !PageLRU page could be in the
	 * process of being released, reclaimed, migrated or on a remote
	 * pagevec that is currently being drained. Furthermore, marking
	 * a remote pagevec's page PageActive potentially hits a race where
	 * a page is marked PageActive just after it is added to the inactive
	 * list causing accounting errors and BUG_ON checks to trigger.
	 */
	for (i = pagevec_count(pvec) - 1; i >= 0; i--) {
		struct page *pagevec_page = pvec->pages[i];

		if (pagevec_page == page) {
			SetPageActive(page);
			break;
		}
	}

	put_cpu_var(lru_add_pvec);
}

/*
 * Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 *
 * When a newly allocated page is not yet visible, so safe for non-atomic ops,
 * __SetPageReferenced(page) may be substituted for mark_page_accessed(page).
 */
void mark_page_accessed(struct page *page)
{
	if (!PageActive(page) && !PageUnevictable(page) &&
			PageReferenced(page)) {

		/*
		 * If the page is on the LRU, queue it for activation via
		 * activate_page_pvecs. Otherwise, assume the page is on a
		 * pagevec, mark it active and it'll be moved to the active
		 * LRU on the next drain.
		 */
		if (PageLRU(page))
			activate_page(page);
		else
			__lru_cache_activate_page(page);
		ClearPageReferenced(page);
		if (page_is_file_cache(page))
			workingset_activation(page);
	} else if (!PageReferenced(page)) {
		SetPageReferenced(page);
	}
}
EXPORT_SYMBOL(mark_page_accessed);

// 添加指定的内存页到 pagevec 中，如果 pagevec 中的页已经达到了设定的阈值
// 则把 pagevec 中的页批量的、一起添加到与其对应的 zone.lruvec[i] 链表中
static void __lru_cache_add(struct page *page)
{
	struct pagevec *pvec = &get_cpu_var(lru_add_pvec);

	page_cache_get(page);
	if (!pagevec_space(pvec))
		__pagevec_lru_add(pvec);
	pagevec_add(pvec, page);
	put_cpu_var(lru_add_pvec);
}

/**
 * lru_cache_add: add a page to the page lists
 * @page: the page to add
 */
void lru_cache_add_anon(struct page *page)
{
	if (PageActive(page))
		ClearPageActive(page);
	__lru_cache_add(page);
}

void lru_cache_add_file(struct page *page)
{
	if (PageActive(page))
		ClearPageActive(page);
	__lru_cache_add(page);
}
EXPORT_SYMBOL(lru_cache_add_file);

/**
 * lru_cache_add - add a page to a page list
 * @page: the page to be added to the LRU.
 *
 * Queue the page for addition to the LRU via pagevec. The decision on whether
 * to add the page to the [in]active [file|anon] list is deferred until the
 * pagevec is drained. This gives a chance for the caller of lru_cache_add()
 * have the page added to the active list using mark_page_accessed().
 */
void lru_cache_add(struct page *page)
{
	VM_BUG_ON_PAGE(PageActive(page) && PageUnevictable(page), page);
	VM_BUG_ON_PAGE(PageLRU(page), page);
	__lru_cache_add(page);
}

/**
 * add_page_to_unevictable_list - add a page to the unevictable list
 * @page:  the page to be added to the unevictable list
 *
 * Add page directly to its zone's unevictable list.  To avoid races with
 * tasks that might be making the page evictable, through eg. munlock,
 * munmap or exit, while it's not on the lru, we want to add the page
 * while it's locked or otherwise "invisible" to other tasks.  This is
 * difficult to do when using the pagevec cache, so bypass that.
 */
void add_page_to_unevictable_list(struct page *page)
{
	struct zone *zone = page_zone(page);
	struct lruvec *lruvec;

	spin_lock_irq(&zone->lru_lock);
	lruvec = mem_cgroup_page_lruvec(page, zone);
	ClearPageActive(page);
	SetPageUnevictable(page);
	SetPageLRU(page);
	add_page_to_lru_list(page, lruvec, LRU_UNEVICTABLE);
	spin_unlock_irq(&zone->lru_lock);
}

/**
 * lru_cache_add_active_or_unevictable
 * @page:  the page to be added to LRU
 * @vma:   vma in which page is mapped for determining reclaimability
 *
 * Place @page on the active or unevictable LRU list, depending on its
 * evictability.  Note that if the page is not evictable, it goes
 * directly back onto it's zone's unevictable list, it does NOT use a
 * per cpu pagevec.
 */
void lru_cache_add_active_or_unevictable(struct page *page,
					 struct vm_area_struct *vma)
{
	VM_BUG_ON_PAGE(PageLRU(page), page);

	if (likely((vma->vm_flags & (VM_LOCKED | VM_SPECIAL)) != VM_LOCKED)) {
		SetPageActive(page);
		lru_cache_add(page);
		return;
	}

	if (!TestSetPageMlocked(page)) {
		/*
		 * We use the irq-unsafe __mod_zone_page_stat because this
		 * counter is not modified from interrupt context, and the pte
		 * lock is held(spinlock), which implies preemption disabled.
		 */
		__mod_zone_page_state(page_zone(page), NR_MLOCK,
				    hpage_nr_pages(page));
		count_vm_event(UNEVICTABLE_PGMLOCKED);
	}
	add_page_to_unevictable_list(page);
}

/*
 * If the page can not be invalidated, it is moved to the
 * inactive list to speed up its reclaim.  It is moved to the
 * head of the list, rather than the tail, to give the flusher
 * threads some time to write it out, as this is much more
 * effective than the single-page writeout from reclaim.
 *
 * If the page isn't page_mapped and dirty/writeback, the page
 * could reclaim asap using PG_reclaim.
 *
 * 1. active, mapped page -> none
 * 2. active, dirty/writeback page -> inactive, head, PG_reclaim
 * 3. inactive, mapped page -> none
 * 4. inactive, dirty/writeback page -> inactive, head, PG_reclaim
 * 5. inactive, clean -> inactive, tail
 * 6. Others -> none
 *
 * In 4, why it moves inactive's head, the VM expects the page would
 * be write it out by flusher threads as this is much more effective
 * than the single-page writeout from reclaim.
 */
// 把指定的内存页添加到指定 lru 向量链表的 inactive（匿名或者文件）链表上
// 如果这个页需要回写或是 dirty 状态，则添加到对应链表的头部，如果这个内存页
// 是干净的，则把这个内存页添加到对应链表的尾部，这样可以尽快回收（因为回收
// 内存时是从链表尾部开始扫描的）
static void lru_deactivate_fn(struct page *page, struct lruvec *lruvec,
			      void *arg)
{
	int lru, file;
	bool active;

	// 判断指定的内存页是否在 lru 链表上
	if (!PageLRU(page))
		return;

	// 判断指定的内存页是否为 unevictable 的
	if (PageUnevictable(page))
		return;

	/* Some processes are using the page */
	// 判断指定内存页是否被其他进程映射使用了
	if (page_mapped(page))
		return;

	active = PageActive(page);
	file = page_is_file_cache(page);
	lru = page_lru_base_type(page);

	// 把指定的页从 lru active 或者 lru inactive 链表上移除
	del_page_from_lru_list(page, lruvec, lru + active);
	
	ClearPageActive(page);
	ClearPageReferenced(page);

	// 把指定的页添加到         lru inactive 链表头部位置上
	add_page_to_lru_list(page, lruvec, lru);

	if (PageWriteback(page) || PageDirty(page)) {
		/*
		 * PG_reclaim could be raced with end_page_writeback
		 * It can make readahead confusing.  But race window
		 * is _really_ small and  it's non-critical problem.
		 */
		SetPageReclaim(page);
	} else {
		/*
		 * The page's writeback ends up during pagevec
		 * We moves tha page into tail of inactive.
		 */
		// 因为在进行内存回收的时候，是从 inactive lru 链表末尾开始扫描，
		// 所以如果这个内存页是个干净的页，表示可以直接回收，我们直接把
		// 这个内存页放到 inactive lru 链表的末尾
		list_move_tail(&page->lru, &lruvec->lists[lru]);
		__count_vm_event(PGROTATED);
	}

	if (active)
		__count_vm_event(PGDEACTIVATE);
	
	update_page_reclaim_stat(lruvec, file, 0);
}

/*
 * Drain pages out of the cpu's pagevecs.
 * Either "cpu" is the current CPU, and preemption has already been
 * disabled; or "cpu" is being hot-unplugged, and is already dead.
 */
// 从这个函数可以看出系统中一共有几种内存页向量做缓存，分别如下：
// 1. lru_add_pvec：当一个不在 lru 链表上的页要向 lru 链表中添加的时候，就把这个页添加到这个缓存中
// 2. lru_rotate_pvecs：在这个内存页向量中的页已经在 lru 链表中了，当我们要把 inactive（匿名或者文件）
//    链表上的内存页移动到这个链表的末尾的时候，就把这个页添加到这个缓存中
// 3. lru_deactivate_pvecs：在这个内存页向量中的页已经在 lru 链表中了，当我们需要把一个指定的内存页
//    标记为 inactive 状态时，就把这个页添加到这个缓存中
// 4. activate_page_pvecs：在这个内存页向量中的页已经在 lru 链表中了，当我们要把一个指定的内存页标记
//    为 active 状态时，就把这个页添加到这个缓存中
void lru_add_drain_cpu(int cpu)
{
	struct pagevec *pvec = &per_cpu(lru_add_pvec, cpu);

	// 如果本地 cpu 的 lru_add 页向量（lru_add_pvec）不为空，则把这些内存页
	// 全部添加到 lru 向量链表中
	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);

	// 如果本地 cpu 的 lru_rotate 页向量（lru_rotate_pvecs）不为空，那么依次遍历这个
	// 链表上的每一个内存页，如果内存页的状态为 inactive，我们就把这个内存页移动到
	// 与其对应的 inactive lru（匿名或者文件）链表的末尾位置
	pvec = &per_cpu(lru_rotate_pvecs, cpu);
	if (pagevec_count(pvec)) {
		unsigned long flags;

		/* No harm done if a racing interrupt already did this */
		local_irq_save(flags);
		pagevec_move_tail(pvec);
		local_irq_restore(flags);
	}
	
	// 如果本地 cpu 的 lru_deactivate 页向量（lru_deactivate_pvecs）不为空，那么依次
	// 遍历这个链表上的每一个内存页，并把这些内存页添加到与其对应（匿名或者文件）的
	// inactive 链表上
	pvec = &per_cpu(lru_deactivate_pvecs, cpu);
	if (pagevec_count(pvec))
		pagevec_lru_move_fn(pvec, lru_deactivate_fn, NULL);

	// 如果本地 cpu 的 activate_page 页向量（activate_page_pvecs）不为空，那么依次
	// 遍历这个链表上的每一个内存页，并将其从  inactive lru 链表中移除，并添加到与其
	// 对应的 active lru 链表上
	activate_page_drain(cpu);
}

/**
 * deactivate_page - forcefully deactivate a page
 * @page: page to deactivate
 *
 * This function hints the VM that @page is a good reclaim candidate,
 * for example if its invalidation fails due to the page being dirty
 * or under writeback.
 */
void deactivate_page(struct page *page)
{
	/*
	 * In a workload with many unevictable page such as mprotect, unevictable
	 * page deactivation for accelerating reclaim is pointless.
	 */
	if (PageUnevictable(page))
		return;

	if (likely(get_page_unless_zero(page))) {
		struct pagevec *pvec = &get_cpu_var(lru_deactivate_pvecs);

		if (!pagevec_add(pvec, page))
			pagevec_lru_move_fn(pvec, lru_deactivate_fn, NULL);
		put_cpu_var(lru_deactivate_pvecs);
	}
}

// 把系统中所有的页向量缓存中的内存页刷新到与其对应的 lru 链表上
void lru_add_drain(void)
{
	lru_add_drain_cpu(get_cpu());
	put_cpu();
}

static void lru_add_drain_per_cpu(struct work_struct *dummy)
{
	lru_add_drain();
}

static DEFINE_PER_CPU(struct work_struct, lru_add_drain_work);

void lru_add_drain_all(void)
{
	static DEFINE_MUTEX(lock);
	static struct cpumask has_work;
	int cpu;

	mutex_lock(&lock);
	get_online_cpus();
	cpumask_clear(&has_work);

	for_each_online_cpu(cpu) {
		struct work_struct *work = &per_cpu(lru_add_drain_work, cpu);

		if (pagevec_count(&per_cpu(lru_add_pvec, cpu)) ||
		    pagevec_count(&per_cpu(lru_rotate_pvecs, cpu)) ||
		    pagevec_count(&per_cpu(lru_deactivate_pvecs, cpu)) ||
		    need_activate_page_drain(cpu)) {
			INIT_WORK(work, lru_add_drain_per_cpu);
			schedule_work_on(cpu, work);
			cpumask_set_cpu(cpu, &has_work);
		}
	}

	for_each_cpu(cpu, &has_work)
		flush_work(&per_cpu(lru_add_drain_work, cpu));

	put_online_cpus();
	mutex_unlock(&lock);
}

/**
 * release_pages - batched page_cache_release()
 * @pages: array of pages to release
 * @nr: number of pages
 * @cold: whether the pages are cache cold
 *
 * Decrement the reference count on all the pages in @pages.  If it
 * fell to zero, remove the page from the LRU and free it.
 */
void release_pages(struct page **pages, int nr, bool cold)
{
	int i;
	LIST_HEAD(pages_to_free);
	struct zone *zone = NULL;
	struct lruvec *lruvec;
	unsigned long uninitialized_var(flags);
	unsigned int uninitialized_var(lock_batch);

	for (i = 0; i < nr; i++) {
		struct page *page = pages[i];

		if (unlikely(PageCompound(page))) {
			if (zone) {
				spin_unlock_irqrestore(&zone->lru_lock, flags);
				zone = NULL;
			}
			put_compound_page(page);
			continue;
		}

		/*
		 * Make sure the IRQ-safe lock-holding time does not get
		 * excessive with a continuous string of pages from the
		 * same zone. The lock is held only if zone != NULL.
		 */
		if (zone && ++lock_batch == SWAP_CLUSTER_MAX) {
			spin_unlock_irqrestore(&zone->lru_lock, flags);
			zone = NULL;
		}

		if (!put_page_testzero(page))
			continue;

		if (PageLRU(page)) {
			struct zone *pagezone = page_zone(page);

			if (pagezone != zone) {
				if (zone)
					spin_unlock_irqrestore(&zone->lru_lock,
									flags);
				lock_batch = 0;
				zone = pagezone;
				spin_lock_irqsave(&zone->lru_lock, flags);
			}

			lruvec = mem_cgroup_page_lruvec(page, zone);
			VM_BUG_ON_PAGE(!PageLRU(page), page);
			__ClearPageLRU(page);
			del_page_from_lru_list(page, lruvec, page_off_lru(page));
		}

		/* Clear Active bit in case of parallel mark_page_accessed */
		__ClearPageActive(page);

		list_add(&page->lru, &pages_to_free);
	}
	if (zone)
		spin_unlock_irqrestore(&zone->lru_lock, flags);

	mem_cgroup_uncharge_list(&pages_to_free);
	free_hot_cold_page_list(&pages_to_free, cold);
}
EXPORT_SYMBOL(release_pages);

/*
 * The pages which we're about to release may be in the deferred lru-addition
 * queues.  That would prevent them from really being freed right now.  That's
 * OK from a correctness point of view but is inefficient - those pages may be
 * cache-warm and we want to give them back to the page allocator ASAP.
 *
 * So __pagevec_release() will drain those queues here.  __pagevec_lru_add()
 * and __pagevec_lru_add_active() call release_pages() directly to avoid
 * mutual recursion.
 */
void __pagevec_release(struct pagevec *pvec)
{
	lru_add_drain();
	release_pages(pvec->pages, pagevec_count(pvec), pvec->cold);
	pagevec_reinit(pvec);
}
EXPORT_SYMBOL(__pagevec_release);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/* used by __split_huge_page_refcount() */
void lru_add_page_tail(struct page *page, struct page *page_tail,
		       struct lruvec *lruvec, struct list_head *list)
{
	const int file = 0;

	VM_BUG_ON_PAGE(!PageHead(page), page);
	VM_BUG_ON_PAGE(PageCompound(page_tail), page);
	VM_BUG_ON_PAGE(PageLRU(page_tail), page);
	VM_BUG_ON(NR_CPUS != 1 &&
		  !spin_is_locked(&lruvec_zone(lruvec)->lru_lock));

	if (!list)
		SetPageLRU(page_tail);

	if (likely(PageLRU(page)))
		list_add_tail(&page_tail->lru, &page->lru);
	else if (list) {
		/* page reclaim is reclaiming a huge page */
		get_page(page_tail);
		list_add_tail(&page_tail->lru, list);
	} else {
		struct list_head *list_head;
		/*
		 * Head page has not yet been counted, as an hpage,
		 * so we must account for each subpage individually.
		 *
		 * Use the standard add function to put page_tail on the list,
		 * but then correct its position so they all end up in order.
		 */
		add_page_to_lru_list(page_tail, lruvec, page_lru(page_tail));
		list_head = page_tail->lru.prev;
		list_move_tail(&page_tail->lru, list_head);
	}

	if (!PageUnevictable(page))
		update_page_reclaim_stat(lruvec, file, PageActive(page_tail));
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

// 把指定的内存页添加到指定的 lru 向量链表中，而这个页被添加到 lru 向量链表的
// 哪个 lru 链表中是由这个内存页自身决定的（由 flag 信息计算得出）
static void __pagevec_lru_add_fn(struct page *page, struct lruvec *lruvec,
				 void *arg)
{
	int file = page_is_file_cache(page);
	int active = PageActive(page);
	enum lru_list lru = page_lru(page);

	VM_BUG_ON_PAGE(PageLRU(page), page);

	// 设置内存页的 lru 标志，表示这个内存页在 lru 链表上
	SetPageLRU(page);

	// 添加指定的内存页到制定的 lru 链表上
	add_page_to_lru_list(page, lruvec, lru);

	// 更新 lru 链表中，scanned 链表和 rotated 链表中的成员计数
	update_page_reclaim_stat(lruvec, file, active);
	trace_mm_lru_insertion(page, lru);
}

/*
 * Add the passed pages to the LRU, then drop the caller's refcount
 * on them.  Reinitialises the caller's pagevec.
 */
void __pagevec_lru_add(struct pagevec *pvec)
{
	pagevec_lru_move_fn(pvec, __pagevec_lru_add_fn, NULL);
}
EXPORT_SYMBOL(__pagevec_lru_add);

/**
 * pagevec_lookup_entries - gang pagecache lookup
 * @pvec:	Where the resulting entries are placed
 * @mapping:	The address_space to search
 * @start:	The starting entry index
 * @nr_entries:	The maximum number of entries
 * @indices:	The cache indices corresponding to the entries in @pvec
 *
 * pagevec_lookup_entries() will search for and return a group of up
 * to @nr_entries pages and shadow entries in the mapping.  All
 * entries are placed in @pvec.  pagevec_lookup_entries() takes a
 * reference against actual pages in @pvec.
 *
 * The search returns a group of mapping-contiguous entries with
 * ascending indexes.  There may be holes in the indices due to
 * not-present entries.
 *
 * pagevec_lookup_entries() returns the number of entries which were
 * found.
 */
unsigned pagevec_lookup_entries(struct pagevec *pvec,
				struct address_space *mapping,
				pgoff_t start, unsigned nr_pages,
				pgoff_t *indices)
{
	pvec->nr = find_get_entries(mapping, start, nr_pages,
				    pvec->pages, indices);
	return pagevec_count(pvec);
}

/**
 * pagevec_remove_exceptionals - pagevec exceptionals pruning
 * @pvec:	The pagevec to prune
 *
 * pagevec_lookup_entries() fills both pages and exceptional radix
 * tree entries into the pagevec.  This function prunes all
 * exceptionals from @pvec without leaving holes, so that it can be
 * passed on to page-only pagevec operations.
 */
void pagevec_remove_exceptionals(struct pagevec *pvec)
{
	int i, j;

	for (i = 0, j = 0; i < pagevec_count(pvec); i++) {
		struct page *page = pvec->pages[i];
		if (!radix_tree_exceptional_entry(page))
			pvec->pages[j++] = page;
	}
	pvec->nr = j;
}

/**
 * pagevec_lookup - gang pagecache lookup
 * @pvec:	Where the resulting pages are placed
 * @mapping:	The address_space to search
 * @start:	The starting page index
 * @nr_pages:	The maximum number of pages
 *
 * pagevec_lookup() will search for and return a group of up to @nr_pages pages
 * in the mapping.  The pages are placed in @pvec.  pagevec_lookup() takes a
 * reference against the pages in @pvec.
 *
 * The search returns a group of mapping-contiguous pages with ascending
 * indexes.  There may be holes in the indices due to not-present pages.
 *
 * pagevec_lookup() returns the number of pages which were found.
 */
unsigned pagevec_lookup(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t start, unsigned nr_pages)
{
	pvec->nr = find_get_pages(mapping, start, nr_pages, pvec->pages);
	return pagevec_count(pvec);
}
EXPORT_SYMBOL(pagevec_lookup);

unsigned pagevec_lookup_tag(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t *index, int tag, unsigned nr_pages)
{
	pvec->nr = find_get_pages_tag(mapping, index, tag,
					nr_pages, pvec->pages);
	return pagevec_count(pvec);
}
EXPORT_SYMBOL(pagevec_lookup_tag);

/*
 * Perform any setup for the swap system
 */
void __init swap_setup(void)
{
	unsigned long megs = totalram_pages >> (20 - PAGE_SHIFT);
#ifdef CONFIG_SWAP
	int i;

	for (i = 0; i < MAX_SWAPFILES; i++)
		spin_lock_init(&swapper_spaces[i].tree_lock);
#endif

	/* Use a smaller cluster for small-memory machines */
	if (megs < 16)
		page_cluster = 2;
	else
		page_cluster = 3;
	/*
	 * Right now other parts of the system means that we
	 * _really_ don't want to cluster much more
	 */
}
