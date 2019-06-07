/*
 * include/linux/pagevec.h
 *
 * In many places it is efficient to batch an operation up against multiple
 * pages.  A pagevec is a multipage container which is used for that.
 */

#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

/* 14 pointers + two long's align the pagevec structure to a power of two */
#define PAGEVEC_SIZE	14

struct page;
struct address_space;

// 需要修改 lru 链表时，一定要占有 zone 中的 lru_lock 这个锁，在多核的硬件环境中
// 在同时需要对 lru 链表进行修改时，锁的竞争会非常的频繁，所以内核提供了一个 lru
// 缓存的机制，这种机制能够减少锁的竞争频率。其实这种机制非常简单，lru 缓存相当于
// 将一些需要相同处理的页集合起来，当达到一定数量时再对它们进行一批次的处理，这样
// 做可以让对锁的需求集中在这个处理的时间点，而没有 lru 缓存的情况下，则是当一个
// 页需要处理时则立即进行处理，对锁的需求的时间点就会比较离散

// 当我们把一个内存页从某个 lru 链表移动到某个 lru 缓存中的时候，并没有修改这个
// 页的状态（active 或者 inactive 状态），而是在 lru 缓存满时、把 lru 缓存上的页
// 移动到与其对应的 lru 链表上时，才会设置这些页的状态为对应 lru 链表的状态（active
// 或者 inactive 状态）
struct pagevec {
	unsigned long nr;  // 当前有效的页数
	unsigned long cold;
	struct page *pages[PAGEVEC_SIZE]; // 指针数组，每一项都可以指向一个页描述符，默认大小是 14
};

void __pagevec_release(struct pagevec *pvec);
void __pagevec_lru_add(struct pagevec *pvec);
unsigned pagevec_lookup_entries(struct pagevec *pvec,
				struct address_space *mapping,
				pgoff_t start, unsigned nr_entries,
				pgoff_t *indices);
void pagevec_remove_exceptionals(struct pagevec *pvec);
unsigned pagevec_lookup(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t start, unsigned nr_pages);
unsigned pagevec_lookup_tag(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *index, int tag,
		unsigned nr_pages);

static inline void pagevec_init(struct pagevec *pvec, int cold)
{
	pvec->nr = 0;
	pvec->cold = cold;
}

static inline void pagevec_reinit(struct pagevec *pvec)
{
	pvec->nr = 0;
}

static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}

static inline unsigned pagevec_space(struct pagevec *pvec)
{
	return PAGEVEC_SIZE - pvec->nr;
}

/*
 * Add a page to a pagevec.  Returns the number of slots still available.
 */
static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return pagevec_space(pvec);
}

static inline void pagevec_release(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_release(pvec);
}

#endif /* _LINUX_PAGEVEC_H */
