#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <linux/init.h>
#include <linux/seqlock.h>
#include <linux/nodemask.h>
#include <linux/pageblock-flags.h>
#include <linux/page-flags-layout.h>
#include <linux/atomic.h>
#include <asm/page.h>

/* Free memory management - zoned buddy allocator.  */
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER - 1))

/*
 * PAGE_ALLOC_COSTLY_ORDER is the order at which allocations are deemed
 * costly to service.  That is between allocation orders which should
 * coalesce naturally under reasonable reclaim pressure and those which
 * will not.
 */
#define PAGE_ALLOC_COSTLY_ORDER 3

// 根据内存可移动属性对内存进行分类，这个在内存回收和内存规整模块中会用到
// 主要是为了防治内存管理系统的内存碎片化问题
enum {
	// 这类页在内存中有固定的位置，分配后不可以移动，主要用于内核的核心分配内存
	MIGRATE_UNMOVABLE,   

	// 这类页在内存中分配后也不可移动，但其内容页可以删除并从其他位置重新生成，文件映射就是这一类的
	MIGRATE_RECLAIMABLE, 
	
	// 这类页在内存中分配后位置可随意移动，用户空间应用程序的内存映射属于这一类
	MIGRATE_MOVABLE,     

	// 用来表示每 CPU 页框高速缓存的数据结构中的链表的迁移类型数目
	MIGRATE_PCPTYPES,	/* the number of types on the pcp(struct per_cpu_pageset) lists */

	// 在前三种的列表中都没用可满足分配的内存块时，就可以从 MIGRATE_RESERVE 中分配
	MIGRATE_RESERVE = MIGRATE_PCPTYPES, 
#ifdef CONFIG_CMA
	/*
	 * MIGRATE_CMA migration type is designed to mimic the way
	 * ZONE_MOVABLE works.  Only movable pages can be allocated
	 * from MIGRATE_CMA pageblocks and page allocator never
	 * implicitly change migration type of MIGRATE_CMA pageblock.
	 *
	 * The way to use it is to change migratetype of a range of
	 * pageblocks to MIGRATE_CMA which can be done by
	 * __free_pageblock_cma() function.  What is important though
	 * is that a range of pageblocks must be aligned to
	 * MAX_ORDER_NR_PAGES should biggest page be bigger then
	 * a single pageblock.
	 */
	// 预留一段的内存给驱动使用，但当驱动不用的时候，伙伴系统可以分配给用户进程用作
	// 匿名内存或者页缓存。而当驱动需要使用时，就将进程占用的内存通过回收或者迁移的
	// 方式将之前占用的预留内存腾出来，供驱动使用
	MIGRATE_CMA,
#endif
#ifdef CONFIG_MEMORY_ISOLATION
	// 不能从这个链表分配页框，因为它用于跨越 NUMA 节点移动物理内存页，在大型系统上
	// 它有益于将物理内存页移动到接近于是用该页最频繁地 CPU
	MIGRATE_ISOLATE,	/* can't allocate from here */
#endif
	MIGRATE_TYPES
};

#ifdef CONFIG_CMA
#  define is_migrate_cma(migratetype) unlikely((migratetype) == MIGRATE_CMA)
#else
#  define is_migrate_cma(migratetype) false
#endif

// 遍历伙伴系统中每一阶（按内存块大小划分）内存框以及这阶内存框中
// 每个内存类型（按可移动类型分类）的链表，即遍历内存管理系统的所有链表
#define for_each_migratetype_order(order, type) \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

extern int page_group_by_mobility_disabled;

#define NR_MIGRATETYPE_BITS (PB_migrate_end - PB_migrate + 1)
#define MIGRATETYPE_MASK ((1UL << NR_MIGRATETYPE_BITS) - 1)

#define get_pageblock_migratetype(page)					\
	get_pfnblock_flags_mask(page, page_to_pfn(page),		\
			PB_migrate_end, MIGRATETYPE_MASK)

static inline int get_pfnblock_migratetype(struct page *page, unsigned long pfn)
{
	BUILD_BUG_ON(PB_migrate_end - PB_migrate != 2);
	return get_pfnblock_flags_mask(page, pfn, PB_migrate_end,
					MIGRATETYPE_MASK);
}

// 伙伴系统的一类块（描述 1,2,4,8,16,32,64,128,256,512 或 1024 个连续页框的块）
struct free_area {
	// 在每一类内存块中，会根据内存移动属性对内存进行管理，每一类对应一个链表
	struct list_head	free_list[MIGRATE_TYPES];
	unsigned long		nr_free;
};

struct pglist_data;

/*
 * zone->lock and zone->lru_lock are two of the hottest locks in the kernel.
 * So add a wild amount of padding here to ensure that they fall into separate
 * cachelines.  There are very few zone structures in the machine, so space
 * consumption is not a concern here.
 */
#if defined(CONFIG_SMP)
struct zone_padding {
	char x[0];
} ____cacheline_internodealigned_in_smp;
#define ZONE_PADDING(name)	struct zone_padding name;
#else
#define ZONE_PADDING(name)
#endif

enum zone_stat_item {
	/* First 128 byte cacheline (assuming 64 bit words) */
	NR_FREE_PAGES,
	NR_ALLOC_BATCH,
	NR_LRU_BASE,
	NR_INACTIVE_ANON = NR_LRU_BASE, /* must match order of LRU_[IN]ACTIVE */
	NR_ACTIVE_ANON,		/*  "     "     "   "       "         */
	NR_INACTIVE_FILE,	/*  "     "     "   "       "         */
	NR_ACTIVE_FILE,		/*  "     "     "   "       "         */
	NR_UNEVICTABLE,		/*  "     "     "   "       "         */
	NR_MLOCK,		/* mlock()ed pages found and moved off LRU */
	NR_ANON_PAGES,	/* Mapped anonymous pages */
	NR_FILE_MAPPED,	/* pagecache pages mapped into pagetables.
			   only modified from process context */
	NR_FILE_PAGES,  // 为文件映射划分的内存页数
	NR_FILE_DIRTY,  // 文件映射中处于 dirty 状态的内存页数
	NR_WRITEBACK,
	NR_SLAB_RECLAIMABLE,  // slab 分配器中可回收的内存页数
	NR_SLAB_UNRECLAIMABLE,
	NR_PAGETABLE,		/* used for pagetables */
	NR_KERNEL_STACK,
	/* Second 128 byte cacheline */
	NR_UNSTABLE_NFS,	/* NFS unstable pages */
	NR_BOUNCE,
	NR_VMSCAN_WRITE,
	NR_VMSCAN_IMMEDIATE,	/* Prioritise for reclaim when writeback ends */
	NR_WRITEBACK_TEMP,	/* Writeback using temporary buffers */

	// 在内存时候的时候，我们首先把扫描到的内存页放到 isolate 链表上（临时变量）
	// 然后在对每一个内存页处理，这连个位置记录了在 isolate 链表上的内存页个数
	NR_ISOLATED_ANON,	/* Temporary isolated pages from anon lru */
	NR_ISOLATED_FILE,	/* Temporary isolated pages from file lru */
	
	NR_SHMEM,		/* shmem pages (included tmpfs/GEM pages) */
	NR_DIRTIED,		/* page dirtyings since bootup */
	NR_WRITTEN,		/* page writings since bootup */
	
	// 内存回收的时候，我们需要扫描 lru 链表，NR_PAGES_SCANNED 统计我们在
	// 链表上一共扫描到但是还没开始回收的内存页个数，如果执行了回收操作
	// 则需要从 NR_PAGES_SCANNED 中减去回收的内存页数，只保留没回收的那部分
	NR_PAGES_SCANNED,	/* pages scanned since last reclaim */
	
#ifdef CONFIG_NUMA
	NUMA_HIT,		/* allocated in intended node */
	NUMA_MISS,		/* allocated in non intended node */
	NUMA_FOREIGN,		/* was intended here, hit elsewhere */
	NUMA_INTERLEAVE_HIT,	/* interleaver preferred this zone */
	NUMA_LOCAL,		/* allocation from local node */
	NUMA_OTHER,		/* allocation from other node */
#endif
	WORKINGSET_REFAULT,
	WORKINGSET_ACTIVATE,
	WORKINGSET_NODERECLAIM,
	NR_ANON_TRANSPARENT_HUGEPAGES,
	NR_FREE_CMA_PAGES,
	NR_VM_ZONE_STAT_ITEMS };

/*
 * We do arithmetic on the LRU lists in various places in the code,
 * so it is important to keep the active lists LRU_ACTIVE higher in
 * the array than the corresponding inactive lists, and to keep
 * the *_FILE lists LRU_FILE higher than the corresponding _ANON lists.
 *
 * This has to be kept in sync with the statistics in zone_stat_item
 * above and the descriptions in vmstat_text in mm/vmstat.c
 */
#define LRU_BASE 0
#define LRU_ACTIVE 1
#define LRU_FILE 2

enum lru_list {
	// 对于 zone 中所有可以存放到 swap 分区中并且没被锁在内存中的页
	// (进程堆、栈、数据段使用的页，匿名mmap共享内存使用的页，shmem共享内存使用的页)
	// lru 链表描述符会使用下面两个链表进行组织，这两个链表我们统称为匿名页lru链表
	LRU_INACTIVE_ANON = LRU_BASE,  // 最近没被访问过的并且可以存放到swap分区的页描述符，在此链表中的页描述符的 PG_active 标志为 0
	LRU_ACTIVE_ANON = LRU_BASE + LRU_ACTIVE,  // 最近被访问过的并且不可以存放到 swap 分区的页描述符，此链表中的页描述符的 PG_active 标志为 1

	// 对于 zone 中所有映射了具体磁盘文件页并且没有被锁在内存中的页(映射了内核映像的页除外)
	// lru 链表描述符会使用下面两个链表组织，这两个链表我们统称为文件页lru链表
	LRU_INACTIVE_FILE = LRU_BASE + LRU_FILE,  // 最近没被访问过的文件页的页描述符，此链表中的页描述符的 PG_active 标志为 0
	LRU_ACTIVE_FILE = LRU_BASE + LRU_FILE + LRU_ACTIVE,  // 最近被访问过的文件页的页描述符，此链表中的页描述符的 PG_active 标志为 1

	// 对于 zone 中那些锁在内存中的页，lru 链表描述符会使用这个链表进行组织，表示禁止换出的页的描述符
	LRU_UNEVICTABLE,
	
	NR_LRU_LISTS
};

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

#define for_each_evictable_lru(lru) for (lru = 0; lru <= LRU_ACTIVE_FILE; lru++)

// 判断指定 lru 索引是否是文件 lru 链表
static inline int is_file_lru(enum lru_list lru)
{
	return (lru == LRU_INACTIVE_FILE || lru == LRU_ACTIVE_FILE);
}

// 判断指定 lru 索引是否是 active lru 链表（匿名的或者文件的）
static inline int is_active_lru(enum lru_list lru)
{
	return (lru == LRU_ACTIVE_ANON || lru == LRU_ACTIVE_FILE);
}

// // 判断指定 lru 索引是否是不可移动的 lru 链表
static inline int is_unevictable_lru(enum lru_list lru)
{
	return (lru == LRU_UNEVICTABLE);
}

struct zone_reclaim_stat {
	/*
	 * The pageout code in vmscan.c keeps track of how many of the
	 * mem/swap backed and file backed pages are referenced.
	 * The higher the rotated/scanned ratio, the more valuable
	 * that cache is.
	 *
	 * The anon LRU stats live in [0], file LRU stats in [1]
	 */
	// 统计 active lru 链表中的成员个数
	unsigned long		recent_rotated[2];

	// 统计在内存回收过程中，我们一共往 isolate 链表上移动过的
	// 内存页数（包括 active 和 inactive 两个链表）
	unsigned long		recent_scanned[2];
};

struct lruvec {
	struct list_head lists[NR_LRU_LISTS];
	struct zone_reclaim_stat reclaim_stat;
#ifdef CONFIG_MEMCG
	struct zone *zone;
#endif
};

/* Mask used at gathering information at once (see memcontrol.c) */
#define LRU_ALL_FILE (BIT(LRU_INACTIVE_FILE) | BIT(LRU_ACTIVE_FILE))
#define LRU_ALL_ANON (BIT(LRU_INACTIVE_ANON) | BIT(LRU_ACTIVE_ANON))
#define LRU_ALL	     ((1 << NR_LRU_LISTS) - 1)

/* Isolate clean file */
#define ISOLATE_CLEAN		((__force isolate_mode_t)0x1)
/* Isolate unmapped file */
#define ISOLATE_UNMAPPED	((__force isolate_mode_t)0x2)
/* Isolate for asynchronous migration */
#define ISOLATE_ASYNC_MIGRATE	((__force isolate_mode_t)0x4)
/* Isolate unevictable pages */
#define ISOLATE_UNEVICTABLE	((__force isolate_mode_t)0x8)

/* LRU Isolation modes. */
typedef unsigned __bitwise__ isolate_mode_t;

enum zone_watermarks {
	WMARK_MIN,
	WMARK_LOW,
	WMARK_HIGH,
	NR_WMARK
};

#define min_wmark_pages(z) (z->watermark[WMARK_MIN])
#define low_wmark_pages(z) (z->watermark[WMARK_LOW])
#define high_wmark_pages(z) (z->watermark[WMARK_HIGH])

// 在每个 cpu 上都会缓存独立的 page 信息，这样可以提高内存管理系统效率
// 当申请内存者只需要一个页框时，内核会从每 CPU 高速缓存中相应类型的单页框
// 链表中获取一个页框交给申请者，这样的好处是，在释放单个页框时会放入每 CPU
// 高速缓存链表，如果这时有需要申请单个页框，就把这个刚刚释放的页框交付出去
// 因为这个页框可能还存在于 cache 中，处理时就可直接处理 cache 而不用把这个
// 页框再放入 cache 中，提高了 cache 的命中率
struct per_cpu_pages {
	// 当前 CPU 高速缓存中页框个数
	int count;		/* number of pages in the list */

	// 当此 CPU 高速缓存中页框个数大于 high，则会将 batch 个页框放回伙伴系统
	int high;		/* high watermark, emptying needed */

	// 在高速缓存中将要添加或被删去的页框个数，当链表中页框数量多个上界时会将
	// batch 个页框放回伙伴系统，当链表中页框数量为 0 时则从伙伴系统中获取 batch 个页框
	int batch;		/* chunk size for buddy add/remove */

	/* Lists of pages, one per migrate type stored on the pcp-lists */
	// 页框的链表，如果需要冷高速缓存，从链表尾开始获取页框，如果需要热高速缓存，从链表头开始获取页框
	struct list_head lists[MIGRATE_PCPTYPES];
};

// 每 CPU 高速缓存描述符，管理了阶数为零的一些内存空间，这样在申请一个页空间
// 的时候就可以直接从 cpu 本地申请，这样会提高内存管理系统效率
struct per_cpu_pageset {
	struct per_cpu_pages pcp;
#ifdef CONFIG_NUMA
	s8 expire;
#endif
#ifdef CONFIG_SMP
	s8 stat_threshold;
	s8 vm_stat_diff[NR_VM_ZONE_STAT_ITEMS];
#endif
};

#endif /* !__GENERATING_BOUNDS.H */

enum zone_type {
#ifdef CONFIG_ZONE_DMA
	/*
	 * ZONE_DMA is used when there are devices that are not able
	 * to do DMA to all of addressable memory (ZONE_NORMAL). Then we
	 * carve out the portion of memory that is needed for these devices.
	 * The range is arch specific.
	 *
	 * Some examples
	 *
	 * Architecture		Limit
	 * ---------------------------
	 * parisc, ia64, sparc	<4G
	 * s390			<2G
	 * arm			Various
	 * alpha		Unlimited or 0-16MB.
	 *
	 * i386, x86_64 and multiple other arches
	 * 			<16M.
	 */
	// 这个 zone 对应着物理内存的前 16MB 空间，因为在一些比较老的设备上
	// DMA 控制器只有 24 bit 地址线，所以可寻址地址空间范围是 16MB
	ZONE_DMA,
#endif
#ifdef CONFIG_ZONE_DMA32
	/*
	 * x86_64 needs two ZONE_DMAs because it supports devices that are
	 * only able to do DMA to the lower 16M but also 32 bit devices that
	 * can only do DMA areas below 4G.
	 */
	// 这个 zone 对应物理内存的前 4GB 空间，适用于那些只有 32 bit 地址线
	// 的 DMA 控制器，即其可寻址范围是 4GB
	ZONE_DMA32,
#endif
	/*
	 * Normal addressable memory is in ZONE_NORMAL. DMA operations can be
	 * performed on pages in ZONE_NORMAL if the DMA devices support
	 * transfers to all addressable memory.
	 */
	ZONE_NORMAL,
#ifdef CONFIG_HIGHMEM
	/*
	 * A memory area that is only addressable by the kernel through
	 * mapping portions into its own address space. This is for example
	 * used by i386 to allow the kernel to address the memory beyond
	 * 900MB. The kernel will set up special mappings (page
	 * table entries on i386) for each page that the kernel needs to
	 * access.
	 */
	// 高端内存、动态映射区域
	ZONE_HIGHMEM,
#endif
	ZONE_MOVABLE,
	__MAX_NR_ZONES
};

#ifndef __GENERATING_BOUNDS_H

// struct zone 数据结构是用来管理一段物理内存的，这个数据结构包括了
// 操作这段物理内存所需的所有信息（地址范围，同步锁...）
struct zone {
	/* Read-mostly fields */

	/* zone watermarks, access with *_wmark_pages(zone) macros */
	// 当前 zone 设置的“不同等级”的水印值，目前从低到高分别是 WMARK_MIN, WMARK_LOW, WMARK_HIGH,
	unsigned long watermark[NR_WMARK];

	/*
	 * We don't know if the memory that we're going to allocate will be freeable
	 * or/and it will be released eventually, so to avoid totally wasting several
	 * GB of ram we must reserve some of the lower zone memory (otherwise we risk
	 * to run OOM on the lower zones despite there's tons of freeable ram
	 * on the higher zones). This array is recalculated at runtime if the
	 * sysctl_lowmem_reserve_ratio sysctl changes.
	 */
	// kernel 在分配内存时，可能会涉及到多个 zone，分配会尝试从 zonelis t第一个 zone 
	// 分配如果失败就会尝试下一个低级的zone（这里的低级仅仅指 zone 内存的位置，实际上
	// 低地址 zone 是更稀缺的资源）。我们可以想像应用进程通过内存映射申请 Highmem 并
	// 且加 mlock 分配，如果此时 Highmem zone 无法满足分配，则会尝试从 Normal 进行分配
	// 这就有一个问题，来自 Highmem 的请求可能会耗尽 Normal zone 的内存，而且由于 mlock
	// 又无法回收，最终的结果就是 Normal zone 无内存提供给 kernel 的正常分配，而 Highmem
	// 有大把的可回收内存无法有效利用。

	// 因此针对这个 case，使得 Normal zone 在碰到来自 Highmem 的分配请求时，可以通过
	// lowmem_reserve 声明：可以使用我的内存，但是必须要保留 lowmem_reserve[NORMAL] 给我
	// 自己使用。同样当从 Normal 失败后，会尝试从 zonelist 中的 DMA 申请分配，通过
	// lowmem_reserve[DMA]，限制来自 HIGHMEM 和 Normal 的分配请求。
	long lowmem_reserve[MAX_NR_ZONES];

#ifdef CONFIG_NUMA
    /* 表示当前 zone 所属 node 的 id 值 */
	int node;
#endif

	/*
	 * The target ratio of ACTIVE_ANON to INACTIVE_ANON pages on
	 * this zone's LRU.  Maintained by the pageout code.
	 */
	unsigned int inactive_ratio;

	struct pglist_data	*zone_pgdat;

	// 每 CPU 高速缓存描述符，管理了阶数为零的一些内存空间，这样在申请一个页空间
	// 的时候就可以直接从 cpu 本地申请，这样会提高内存管理系统效率
	struct per_cpu_pageset __percpu *pageset;

	/*
	 * This is a per-zone reserve of pages that should not be
	 * considered dirtyable memory.
	 */
	unsigned long		dirty_balance_reserve;

#ifndef CONFIG_SPARSEMEM
	/*
	 * Flags for a pageblock_nr_pages block. See pageblock-flags.h.
	 * In SPARSEMEM, this map is stored in struct mem_section
	 */
	unsigned long		*pageblock_flags;
#endif /* CONFIG_SPARSEMEM */

#ifdef CONFIG_NUMA
	/*
	 * zone reclaim becomes active if more unmapped pages exist.
	 */
	unsigned long		min_unmapped_pages;
	unsigned long		min_slab_pages;
#endif /* CONFIG_NUMA */

	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
	// 当前 zone 起始物理内存的页帧号
	unsigned long		zone_start_pfn;

	/*
	 * spanned_pages is the total pages spanned by the zone, including
	 * holes, which is calculated as:
	 * 	spanned_pages = zone_end_pfn - zone_start_pfn;
	 *
	 * present_pages is physical pages existing within the zone, which
	 * is calculated as:
	 *	present_pages = spanned_pages - absent_pages(pages in holes);
	 *
	 * managed_pages is present pages managed by the buddy system, which
	 * is calculated as (reserved_pages includes pages allocated by the
	 * bootmem allocator):
	 *	managed_pages = present_pages - reserved_pages;
	 *
	 * So present_pages may be used by memory hotplug or memory power
	 * management logic to figure out unmanaged pages by checking
	 * (present_pages - managed_pages). And managed_pages should be used
	 * by page allocator and vm scanner to calculate all kinds of watermarks
	 * and thresholds.
	 *
	 * Locking rules:
	 *
	 * zone_start_pfn and spanned_pages are protected by span_seqlock.
	 * It is a seqlock because it has to be read outside of zone->lock,
	 * and it is done in the main allocator path.  But, it is written
	 * quite infrequently.
	 *
	 * The span_seq lock is declared along with zone->lock because it is
	 * frequently read in proximity to zone->lock.  It's good to
	 * give them a chance of being in the same cacheline.
	 *
	 * Write access to present_pages at runtime should be protected by
	 * mem_hotplug_begin/end(). Any reader who can't tolerant drift of
	 * present_pages should get_online_mems() to get a stable value.
	 *
	 * Read access to managed_pages should be safe because it's unsigned
	 * long. Write access to zone->managed_pages and totalram_pages are
	 * protected by managed_page_count_lock at runtime. Idealy only
	 * adjust_managed_page_count() should be used instead of directly
	 * touching zone->managed_pages and totalram_pages.
	 */
	unsigned long		managed_pages; // 当前 zone 给伙伴系统管理的物理页数
	unsigned long		spanned_pages; // 当前 zone 覆盖的物理地址空间范围，包括空洞页
	unsigned long		present_pages; // 当前 zone 中有效的物理页数（实际的物理内存）

	const char		*name;

	/*
	 * Number of MIGRATE_RESERVE page block. To maintain for just
	 * optimization. Protected by zone->lock.
	 */
	int			nr_migrate_reserve_block;

#ifdef CONFIG_MEMORY_ISOLATION
	/*
	 * Number of isolated pageblock. It is used to solve incorrect
	 * freepage counting problem due to racy retrieving migratetype
	 * of pageblock. Protected by zone->lock.
	 */
	unsigned long		nr_isolate_pageblock;
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
	/* see spanned/present_pages for more description */
	seqlock_t		span_seqlock;
#endif

	/*
	 * wait_table		-- the array holding the hash table
	 * wait_table_hash_nr_entries	-- the size of the hash table array
	 * wait_table_bits	-- wait_table_size == (1 << wait_table_bits)
	 *
	 * The purpose of all these is to keep track of the people
	 * waiting for a page to become available and make them
	 * runnable again when possible. The trouble is that this
	 * consumes a lot of space, especially when so few things
	 * wait on pages at a given time. So instead of using
	 * per-page waitqueues, we use a waitqueue hash table.
	 *
	 * The bucket discipline is to sleep on the same queue when
	 * colliding and wake all in that wait queue when removing.
	 * When something wakes, it must check to be sure its page is
	 * truly available, a la thundering herd. The cost of a
	 * collision is great, but given the expected load of the
	 * table, they should be so rare as to be outweighed by the
	 * benefits from the saved space.
	 *
	 * __wait_on_page_locked() and unlock_page() in mm/filemap.c, are the
	 * primary users of these fields, and in mm/page_alloc.c
	 * free_area_init_core() performs the initialization of them.
	 */
	wait_queue_head_t	*wait_table;
	unsigned long		wait_table_hash_nr_entries;
	unsigned long		wait_table_bits;

	ZONE_PADDING(_pad1_)
		
	/* free areas of different sizes */
	// 伙伴系统的每“阶”内存（以页为单位）都用一个 struct free_area 来表示
	// 通过分阶，我们可以把内存分割成固定阶数大小的内存块，用来实现伙伴系统算法
	struct free_area	free_area[MAX_ORDER];

	/* zone flags, see below */
	unsigned long		flags;

	/* Write-intensive fields used from the page allocator */
	spinlock_t		lock;

	ZONE_PADDING(_pad2_)

	/* Write-intensive fields used by page reclaim */

	/* Fields commonly accessed by the page reclaim scanner */
	spinlock_t		lru_lock;
	struct lruvec		lruvec;

	/* Evictions & activations on the inactive file list */
	atomic_long_t		inactive_age;

	/*
	 * When free pages are below this point, additional steps are taken
	 * when reading the number of free pages to avoid per-cpu counter
	 * drift allowing watermarks to be breached
	 */
	unsigned long percpu_drift_mark;

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	/* pfn where compaction free scanner should start */
	unsigned long		compact_cached_free_pfn;
	/* pfn where async and sync compaction migration scanner should start */
	unsigned long		compact_cached_migrate_pfn[2];
#endif

#ifdef CONFIG_COMPACTION
	/*
	 * On compaction failure, 1<<compact_defer_shift compactions
	 * are skipped before trying again. The number attempted since
	 * last failure is tracked with compact_considered.
	 */
	unsigned int		compact_considered;
	unsigned int		compact_defer_shift;
	int			compact_order_failed;
#endif

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	/* Set to true when the PG_migrate_skip bits should be cleared */
	bool			compact_blockskip_flush;
#endif

	ZONE_PADDING(_pad3_)
	/* Zone statistics */
	// 当前 zone 处于不同状态下的统计信息，每个状态用一个独立的变量来统计
	atomic_long_t		vm_stat[NR_VM_ZONE_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

enum zone_flags {
	ZONE_RECLAIM_LOCKED,		/* prevents concurrent reclaim */
	ZONE_OOM_LOCKED,		/* zone is in OOM killer zonelist */
	ZONE_CONGESTED,			/* zone has many dirty pages backed by
					 * a congested BDI
					 */
	ZONE_DIRTY,			/* reclaim scanning has recently found
					 * many dirty file pages at the tail
					 * of the LRU.
					 */
	ZONE_WRITEBACK,			/* reclaim scanning has recently found
					 * many pages under writeback
					 */
	ZONE_FAIR_DEPLETED,		/* fair zone policy batch depleted */
};

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

static inline bool zone_spans_pfn(const struct zone *zone, unsigned long pfn)
{
	return zone->zone_start_pfn <= pfn && pfn < zone_end_pfn(zone);
}

static inline bool zone_is_initialized(struct zone *zone)
{
	return !!zone->wait_table;
}

static inline bool zone_is_empty(struct zone *zone)
{
	return zone->spanned_pages == 0;
}

/*
 * The "priority" of VM scanning is how much of the queues we will scan in one
 * go. A value of 12 for DEF_PRIORITY implies that we will scan 1/4096th of the
 * queues ("queue_length >> 12") during an aging round.
 */
#define DEF_PRIORITY 12

/* Maximum number of zones on a zonelist */
#define MAX_ZONES_PER_ZONELIST (MAX_NUMNODES * MAX_NR_ZONES)

#ifdef CONFIG_NUMA

/*
 * The NUMA zonelists are doubled because we need zonelists that restrict the
 * allocations to a single node for __GFP_THISNODE.
 *
 * [0]	: Zonelist with fallback
 * [1]	: No fallback (__GFP_THISNODE)
 */
#define MAX_ZONELISTS 2


/*
 * We cache key information from each zonelist for smaller cache
 * footprint when scanning for free pages in get_page_from_freelist().
 *
 * 1) The BITMAP fullzones tracks which zones in a zonelist have come
 *    up short of free memory since the last time (last_fullzone_zap)
 *    we zero'd fullzones.
 * 2) The array z_to_n[] maps each zone in the zonelist to its node
 *    id, so that we can efficiently evaluate whether that node is
 *    set in the current tasks mems_allowed.
 *
 * Both fullzones and z_to_n[] are one-to-one with the zonelist,
 * indexed by a zones offset in the zonelist zones[] array.
 *
 * The get_page_from_freelist() routine does two scans.  During the
 * first scan, we skip zones whose corresponding bit in 'fullzones'
 * is set or whose corresponding node in current->mems_allowed (which
 * comes from cpusets) is not set.  During the second scan, we bypass
 * this zonelist_cache, to ensure we look methodically at each zone.
 *
 * Once per second, we zero out (zap) fullzones, forcing us to
 * reconsider nodes that might have regained more free memory.
 * The field last_full_zap is the time we last zapped fullzones.
 *
 * This mechanism reduces the amount of time we waste repeatedly
 * reexaming zones for free memory when they just came up low on
 * memory momentarilly ago.
 *
 * The zonelist_cache struct members logically belong in struct
 * zonelist.  However, the mempolicy zonelists constructed for
 * MPOL_BIND are intentionally variable length (and usually much
 * shorter).  A general purpose mechanism for handling structs with
 * multiple variable length members is more mechanism than we want
 * here.  We resort to some special case hackery instead.
 *
 * The MPOL_BIND zonelists don't need this zonelist_cache (in good
 * part because they are shorter), so we put the fixed length stuff
 * at the front of the zonelist struct, ending in a variable length
 * zones[], as is needed by MPOL_BIND.
 *
 * Then we put the optional zonelist cache on the end of the zonelist
 * struct.  This optional stuff is found by a 'zlcache_ptr' pointer in
 * the fixed length portion at the front of the struct.  This pointer
 * both enables us to find the zonelist cache, and in the case of
 * MPOL_BIND zonelists, (which will just set the zlcache_ptr to NULL)
 * to know that the zonelist cache is not there.
 *
 * The end result is that struct zonelists come in two flavors:
 *  1) The full, fixed length version, shown below, and
 *  2) The custom zonelists for MPOL_BIND.
 * The custom MPOL_BIND zonelists have a NULL zlcache_ptr and no zlcache.
 *
 * Even though there may be multiple CPU cores on a node modifying
 * fullzones or last_full_zap in the same zonelist_cache at the same
 * time, we don't lock it.  This is just hint data - if it is wrong now
 * and then, the allocator will still function, perhaps a bit slower.
 */


struct zonelist_cache {
	unsigned short z_to_n[MAX_ZONES_PER_ZONELIST];		/* zone->nid */

	// 处于 full 状态的 zone 表示内存已经全部分配出去且无法执行内存回收操作
	// 所以这个 zone 不能给其他模块提供内存了
	DECLARE_BITMAP(fullzones, MAX_ZONES_PER_ZONELIST);	/* zone full? */
	unsigned long last_full_zap;		/* when last zap'd (jiffies) */
};
#else
#define MAX_ZONELISTS 1
struct zonelist_cache;
#endif

/*
 * This struct contains information about a zone in a zonelist. It is stored
 * here to avoid dereferences into large structures and lookups of tables
 */
struct zoneref {
	struct zone *zone;	/* Pointer to actual zone */
	int zone_idx;		/* zone_idx(zoneref->zone) */
};

/*
 * One allocation request operates on a zonelist. A zonelist
 * is a list of zones, the first one is the 'goal' of the
 * allocation, the other zones are fallback zones, in decreasing
 * priority.
 *
 * If zlcache_ptr is not NULL, then it is just the address of zlcache,
 * as explained above.  If zlcache_ptr is NULL, there is no zlcache.
 * *
 * To speed the reading of the zonelist, the zonerefs contain the zone index
 * of the entry being read. Helper functions to access information given
 * a struct zoneref are
 *
 * zonelist_zone()	- Return the struct zone * for an entry in _zonerefs
 * zonelist_zone_idx()	- Return the index of the zone for an entry
 * zonelist_node_idx()	- Return the index of the node for an entry
 */
struct zonelist {
	// 如果为 NULL，表示内存是 UMA 模
	struct zonelist_cache *zlcache_ptr;		     // NULL or &zlcache

	// zonelist 中所包含的 zone 对象数组，如果 _zonerefs.zone 和
	// _zonerefs.zone_idx 为空，则表示是一个无效的 zone
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
#ifdef CONFIG_NUMA
	struct zonelist_cache zlcache;			     // optional ...
#endif
};

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
struct node_active_region {
	unsigned long start_pfn;
	unsigned long end_pfn;
	int nid;
};
#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

#ifndef CONFIG_DISCONTIGMEM
/* The array of struct pages - for discontigmem use pgdat->lmem_map */
extern struct page *mem_map;
#endif

/*
 * The pg_data_t structure is used in machines with CONFIG_DISCONTIGMEM
 * (mostly NUMA machines?) to denote a higher-level memory zone than the
 * zone denotes.
 *
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout.
 *
 * Memory statistics and page replacement data structures are maintained on a
 * per-zone basis.
 */
struct bootmem_data;
typedef struct pglist_data {
	struct zone node_zones[MAX_NR_ZONES];

	// 在一个 node 节点中包括的 zonelist 数组列表，现在的 linux 系统中
	// 一个 node 一共包含两个 zonelist，第一个 zonelist 包含了系统中所有
	// 内存的 zone，第二个 zonelist 包含了当前节点的 zone
	// note : 一个 zonelist 中包括多个 zone
	struct zonelist node_zonelists[MAX_ZONELISTS];
	int nr_zones;
#ifdef CONFIG_FLAT_NODE_MEM_MAP	/* means !SPARSEMEM */
	struct page *node_mem_map;
#ifdef CONFIG_PAGE_EXTENSION
	struct page_ext *node_page_ext;
#endif
#endif
#ifndef CONFIG_NO_BOOTMEM
	struct bootmem_data *bdata;
#endif
#ifdef CONFIG_MEMORY_HOTPLUG
	/*
	 * Must be held any time you expect node_start_pfn, node_present_pages
	 * or node_spanned_pages stay constant.  Holding this will also
	 * guarantee that any pfn_valid() stays that way.
	 *
	 * pgdat_resize_lock() and pgdat_resize_unlock() are provided to
	 * manipulate node_size_lock without checking for CONFIG_MEMORY_HOTPLUG.
	 *
	 * Nests above zone->lock and zone->span_seqlock
	 */
	spinlock_t node_size_lock;
#endif
	unsigned long node_start_pfn;
	unsigned long node_present_pages; /* total number of physical pages */
	unsigned long node_spanned_pages; /* total size of physical page
					     range, including holes */
	int node_id;
	wait_queue_head_t kswapd_wait;
	wait_queue_head_t pfmemalloc_wait;
	struct task_struct *kswapd;	/* Protected by
					   mem_hotplug_begin/end() */
	int kswapd_max_order;
	enum zone_type classzone_idx;
#ifdef CONFIG_NUMA_BALANCING
	/* Lock serializing the migrate rate limiting window */
	spinlock_t numabalancing_migrate_lock;

	/* Rate limiting time interval */
	unsigned long numabalancing_migrate_next_window;

	/* Number of pages migrated during the rate limiting time interval */
	unsigned long numabalancing_migrate_nr_pages;
#endif
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)
#ifdef CONFIG_FLAT_NODE_MEM_MAP
#define pgdat_page_nr(pgdat, pagenr)	((pgdat)->node_mem_map + (pagenr))
#else
#define pgdat_page_nr(pgdat, pagenr)	pfn_to_page((pgdat)->node_start_pfn + (pagenr))
#endif
#define nid_page_nr(nid, pagenr) 	pgdat_page_nr(NODE_DATA(nid),(pagenr))

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid) pgdat_end_pfn(NODE_DATA(nid))

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

static inline bool pgdat_is_empty(pg_data_t *pgdat)
{
	return !pgdat->node_start_pfn && !pgdat->node_spanned_pages;
}

#include <linux/memory_hotplug.h>

extern struct mutex zonelists_mutex;
void build_all_zonelists(pg_data_t *pgdat, struct zone *zone);
void wakeup_kswapd(struct zone *zone, int order, enum zone_type classzone_idx);
bool zone_watermark_ok(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx, int alloc_flags);
bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx, int alloc_flags);
enum memmap_context {
	MEMMAP_EARLY,
	MEMMAP_HOTPLUG,
};
extern int init_currently_empty_zone(struct zone *zone, unsigned long start_pfn,
				     unsigned long size,
				     enum memmap_context context);

extern void lruvec_init(struct lruvec *lruvec);

// 获取指定 lruvec 所在的 zone 空间指针
static inline struct zone *lruvec_zone(struct lruvec *lruvec)
{
#ifdef CONFIG_MEMCG
	return lruvec->zone;
#else
	return container_of(lruvec, struct zone, lruvec);
#endif
}

#ifdef CONFIG_HAVE_MEMORY_PRESENT
void memory_present(int nid, unsigned long start, unsigned long end);
#else
static inline void memory_present(int nid, unsigned long start, unsigned long end) {}
#endif

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
int local_memory_node(int node_id);
#else
static inline int local_memory_node(int node_id) { return node_id; };
#endif

#ifdef CONFIG_NEED_NODE_MEMMAP_SIZE
unsigned long __init node_memmap_size_bytes(int, unsigned long, unsigned long);
#endif

/*
 * zone_idx() returns 0 for the ZONE_DMA zone, 1 for the ZONE_NORMAL zone, etc.
 */
#define zone_idx(zone)		((zone) - (zone)->zone_pgdat->node_zones)

static inline int populated_zone(struct zone *zone)
{
	return (!!zone->present_pages);
}

extern int movable_zone;

static inline int zone_movable_is_highmem(void)
{
#if defined(CONFIG_HIGHMEM) && defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP)
	return movable_zone == ZONE_HIGHMEM;
#elif defined(CONFIG_HIGHMEM)
	return (ZONE_MOVABLE - 1) == ZONE_HIGHMEM;
#else
	return 0;
#endif
}

static inline int is_highmem_idx(enum zone_type idx)
{
#ifdef CONFIG_HIGHMEM
	return (idx == ZONE_HIGHMEM ||
		(idx == ZONE_MOVABLE && zone_movable_is_highmem()));
#else
	return 0;
#endif
}

/**
 * is_highmem - helper function to quickly check if a struct zone is a 
 *              highmem zone or not.  This is an attempt to keep references
 *              to ZONE_{DMA/NORMAL/HIGHMEM/etc} in general code to a minimum.
 * @zone - pointer to struct zone variable
 */
static inline int is_highmem(struct zone *zone)
{
#ifdef CONFIG_HIGHMEM
	int zone_off = (char *)zone - (char *)zone->zone_pgdat->node_zones;
	return zone_off == ZONE_HIGHMEM * sizeof(*zone) ||
	       (zone_off == ZONE_MOVABLE * sizeof(*zone) &&
		zone_movable_is_highmem());
#else
	return 0;
#endif
}

/* These two functions are used to setup the per zone pages min values */
struct ctl_table;
int min_free_kbytes_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
extern int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES-1];
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int percpu_pagelist_fraction_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);

extern int numa_zonelist_order_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
extern char numa_zonelist_order[];
#define NUMA_ZONELIST_ORDER_LEN 16	/* string buffer size */

#ifndef CONFIG_NEED_MULTIPLE_NODES

extern struct pglist_data contig_page_data;
#define NODE_DATA(nid)		(&contig_page_data)
#define NODE_MEM_MAP(nid)	mem_map

#else /* CONFIG_NEED_MULTIPLE_NODES */

#include <asm/mmzone.h>

#endif /* !CONFIG_NEED_MULTIPLE_NODES */

extern struct pglist_data *first_online_pgdat(void);
extern struct pglist_data *next_online_pgdat(struct pglist_data *pgdat);
extern struct zone *next_zone(struct zone *zone);

/**
 * for_each_online_pgdat - helper macro to iterate over all online nodes
 * @pgdat - pointer to a pg_data_t variable
 */
#define for_each_online_pgdat(pgdat)			\
	for (pgdat = first_online_pgdat();		\
	     pgdat;					\
	     pgdat = next_online_pgdat(pgdat))
/**
 * for_each_zone - helper macro to iterate over all memory zones
 * @zone - pointer to struct zone variable
 *
 * The user only needs to declare the zone variable, for_each_zone
 * fills it in.
 */
#define for_each_zone(zone)			        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))

#define for_each_populated_zone(zone)		        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))			\
		if (!populated_zone(zone))		\
			; /* do nothing */		\
		else

static inline struct zone *zonelist_zone(struct zoneref *zoneref)
{
	return zoneref->zone;
}

static inline int zonelist_zone_idx(struct zoneref *zoneref)
{
	return zoneref->zone_idx;
}

static inline int zonelist_node_idx(struct zoneref *zoneref)
{
#ifdef CONFIG_NUMA
	/* zone_to_nid not available in this context */
	return zoneref->zone->node;
#else
	return 0;
#endif /* CONFIG_NUMA */
}

/**
 * next_zones_zonelist - Returns the next zone at or below highest_zoneidx within the allowed nodemask using a cursor within a zonelist as a starting point
 * @z - The cursor used as a starting point for the search
 * @highest_zoneidx - The zone index of the highest zone to return
 * @nodes - An optional nodemask to filter the zonelist with
 *
 * This function returns the next zone at or below a given zone index that is
 * within the allowed nodemask using a cursor as the starting point for the
 * search. The zoneref returned is a cursor that represents the current zone
 * being examined. It should be advanced by one before calling
 * next_zones_zonelist again.
 */
struct zoneref *next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes);

/**
 * first_zones_zonelist - Returns the first zone at or below highest_zoneidx within the allowed nodemask in a zonelist
 * @zonelist - The zonelist to search for a suitable zone
 * @highest_zoneidx - The zone index of the highest zone to return
 * @nodes - An optional nodemask to filter the zonelist with
 * @zone - The first suitable zone found is returned via this parameter
 *
 * This function returns the first zone at or below a given zone index that is
 * within the allowed nodemask. The zoneref returned is a cursor that can be
 * used to iterate the zonelist with next_zones_zonelist by advancing it by
 * one before calling.
 */
// 通过 highest_zoneidx 和 nodemask 两个条件来选择合适的内存节点分配内存
// 基本的条件如下：
// 1. zone index 要小于我们指定的 highest_zoneidx
// 2. zone 对应的 node bit 在 nodemask 中要置位
static inline struct zoneref *first_zones_zonelist(struct zonelist *zonelist,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes,
					struct zone **zone)
{
	struct zoneref *z = next_zones_zonelist(zonelist->_zonerefs,
							highest_zoneidx, nodes);
	*zone = zonelist_zone(z);
	return z;
}

/**
 * for_each_zone_zonelist_nodemask - helper macro to iterate over valid zones in a zonelist at or below a given zone index and within a nodemask
 * @zone - The current zone in the iterator
 * @z - The current pointer within zonelist->zones being iterated
 * @zlist - The zonelist being iterated
 * @highidx - The zone index of the highest zone to return
 * @nodemask - Nodemask allowed by the allocator
 *
 * This iterator iterates though all zones at or below a given zone index and
 * within a given nodemask
 */
#define for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (z = first_zones_zonelist(zlist, highidx, nodemask, &zone);	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))			\

/**
 * for_each_zone_zonelist - helper macro to iterate over valid zones in a zonelist at or below a given zone index
 * @zone - The current zone in the iterator
 * @z - The current pointer within zonelist->zones being iterated
 * @zlist - The zonelist being iterated
 * @highidx - The zone index of the highest zone to return
 *
 * This iterator iterates though all zones at or below a given zone index.
 */
#define for_each_zone_zonelist(zone, z, zlist, highidx) \
	for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, NULL)

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>
#endif

#if !defined(CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID) && \
	!defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP)
static inline unsigned long early_pfn_to_nid(unsigned long pfn)
{
	return 0;
}
#endif

#ifdef CONFIG_FLATMEM
#define pfn_to_nid(pfn)		(0)
#endif

#ifdef CONFIG_SPARSEMEM

/*
 * SECTION_SHIFT    		#bits space required to store a section #
 *
 * PA_SECTION_SHIFT		physical address to/from section number
 * PFN_SECTION_SHIFT		pfn to/from section number
 */
#define PA_SECTION_SHIFT	(SECTION_SIZE_BITS)
#define PFN_SECTION_SHIFT	(SECTION_SIZE_BITS - PAGE_SHIFT)

#define NR_MEM_SECTIONS		(1UL << SECTIONS_SHIFT)

#define PAGES_PER_SECTION       (1UL << PFN_SECTION_SHIFT)
#define PAGE_SECTION_MASK	(~(PAGES_PER_SECTION-1))

#define SECTION_BLOCKFLAGS_BITS \
	((1UL << (PFN_SECTION_SHIFT - pageblock_order)) * NR_PAGEBLOCK_BITS)

#if (MAX_ORDER - 1 + PAGE_SHIFT) > SECTION_SIZE_BITS
#error Allocator MAX_ORDER exceeds SECTION_SIZE
#endif

#define pfn_to_section_nr(pfn) ((pfn) >> PFN_SECTION_SHIFT)
#define section_nr_to_pfn(sec) ((sec) << PFN_SECTION_SHIFT)

#define SECTION_ALIGN_UP(pfn)	(((pfn) + PAGES_PER_SECTION - 1) & PAGE_SECTION_MASK)
#define SECTION_ALIGN_DOWN(pfn)	((pfn) & PAGE_SECTION_MASK)

struct page;
struct page_ext;
struct mem_section {
	/*
	 * This is, logically, a pointer to an array of struct
	 * pages.  However, it is stored with some other magic.
	 * (see sparse.c::sparse_init_one_section())
	 *
	 * Additionally during early boot we encode node id of
	 * the location of the section here to guide allocation.
	 * (see sparse.c::memory_present())
	 *
	 * Making it a UL at least makes someone do a cast
	 * before using it wrong.
	 */
	unsigned long section_mem_map;

	/* See declaration of similar field in struct zone */
	unsigned long *pageblock_flags;
#ifdef CONFIG_PAGE_EXTENSION
	/*
	 * If !SPARSEMEM, pgdat doesn't have page_ext pointer. We use
	 * section. (see page_ext.h about this.)
	 */
	struct page_ext *page_ext;
	unsigned long pad;
#endif
	/*
	 * WARNING: mem_section must be a power-of-2 in size for the
	 * calculation and use of SECTION_ROOT_MASK to make sense.
	 */
};

#ifdef CONFIG_SPARSEMEM_EXTREME
#define SECTIONS_PER_ROOT       (PAGE_SIZE / sizeof (struct mem_section))
#else
#define SECTIONS_PER_ROOT	1
#endif

#define SECTION_NR_TO_ROOT(sec)	((sec) / SECTIONS_PER_ROOT)
#define NR_SECTION_ROOTS	DIV_ROUND_UP(NR_MEM_SECTIONS, SECTIONS_PER_ROOT)
#define SECTION_ROOT_MASK	(SECTIONS_PER_ROOT - 1)

#ifdef CONFIG_SPARSEMEM_EXTREME
extern struct mem_section *mem_section[NR_SECTION_ROOTS];
#else
extern struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT];
#endif

static inline struct mem_section *__nr_to_section(unsigned long nr)
{
	if (!mem_section[SECTION_NR_TO_ROOT(nr)])
		return NULL;
	return &mem_section[SECTION_NR_TO_ROOT(nr)][nr & SECTION_ROOT_MASK];
}
extern int __section_nr(struct mem_section* ms);
extern unsigned long usemap_size(void);

/*
 * We use the lower bits of the mem_map pointer to store
 * a little bit of information.  There should be at least
 * 3 bits here due to 32-bit alignment.
 */
#define	SECTION_MARKED_PRESENT	(1UL<<0)
#define SECTION_HAS_MEM_MAP	(1UL<<1)
#define SECTION_MAP_LAST_BIT	(1UL<<2)
#define SECTION_MAP_MASK	(~(SECTION_MAP_LAST_BIT-1))
#define SECTION_NID_SHIFT	2

static inline struct page *__section_mem_map_addr(struct mem_section *section)
{
	unsigned long map = section->section_mem_map;
	map &= SECTION_MAP_MASK;
	return (struct page *)map;
}

static inline int present_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_MARKED_PRESENT));
}

static inline int present_section_nr(unsigned long nr)
{
	return present_section(__nr_to_section(nr));
}

static inline int valid_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_HAS_MEM_MAP));
}

static inline int valid_section_nr(unsigned long nr)
{
	return valid_section(__nr_to_section(nr));
}

static inline struct mem_section *__pfn_to_section(unsigned long pfn)
{
	return __nr_to_section(pfn_to_section_nr(pfn));
}

#ifndef CONFIG_HAVE_ARCH_PFN_VALID
static inline int pfn_valid(unsigned long pfn)
{
	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	return valid_section(__nr_to_section(pfn_to_section_nr(pfn)));
}
#endif

static inline int pfn_present(unsigned long pfn)
{
	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	return present_section(__nr_to_section(pfn_to_section_nr(pfn)));
}

/*
 * These are _only_ used during initialisation, therefore they
 * can use __initdata ...  They could have names to indicate
 * this restriction.
 */
#ifdef CONFIG_NUMA
#define pfn_to_nid(pfn)							\
({									\
	unsigned long __pfn_to_nid_pfn = (pfn);				\
	page_to_nid(pfn_to_page(__pfn_to_nid_pfn));			\
})
#else
#define pfn_to_nid(pfn)		(0)
#endif

#define early_pfn_valid(pfn)	pfn_valid(pfn)
void sparse_init(void);
#else
#define sparse_init()	do {} while (0)
#define sparse_index_init(_sec, _nid)  do {} while (0)
#endif /* CONFIG_SPARSEMEM */

#ifdef CONFIG_NODES_SPAN_OTHER_NODES
bool early_pfn_in_nid(unsigned long pfn, int nid);
#else
#define early_pfn_in_nid(pfn, nid)	(1)
#endif

#ifndef early_pfn_valid
#define early_pfn_valid(pfn)	(1)
#endif

void memory_present(int nid, unsigned long start, unsigned long end);
unsigned long __init node_memmap_size_bytes(int, unsigned long, unsigned long);

/*
 * If it is possible to have holes within a MAX_ORDER_NR_PAGES, then we
 * need to check pfn validility within that MAX_ORDER_NR_PAGES block.
 * pfn_valid_within() should be used in this case; we optimise this away
 * when we have no holes within a MAX_ORDER_NR_PAGES block.
 */
#ifdef CONFIG_HOLES_IN_ZONE
#define pfn_valid_within(pfn) pfn_valid(pfn)
#else
#define pfn_valid_within(pfn) (1)
#endif

#ifdef CONFIG_ARCH_HAS_HOLES_MEMORYMODEL
/*
 * pfn_valid() is meant to be able to tell if a given PFN has valid memmap
 * associated with it or not. In FLATMEM, it is expected that holes always
 * have valid memmap as long as there is valid PFNs either side of the hole.
 * In SPARSEMEM, it is assumed that a valid section has a memmap for the
 * entire section.
 *
 * However, an ARM, and maybe other embedded architectures in the future
 * free memmap backing holes to save memory on the assumption the memmap is
 * never used. The page_zone linkages are then broken even though pfn_valid()
 * returns true. A walker of the full memmap must then do this additional
 * check to ensure the memmap they are looking at is sane by making sure
 * the zone and PFN linkages are still valid. This is expensive, but walkers
 * of the full memmap are extremely rare.
 */
int memmap_valid_within(unsigned long pfn,
					struct page *page, struct zone *zone);
#else
static inline int memmap_valid_within(unsigned long pfn,
					struct page *page, struct zone *zone)
{
	return 1;
}
#endif /* CONFIG_ARCH_HAS_HOLES_MEMORYMODEL */

#endif /* !__GENERATING_BOUNDS.H */
#endif /* !__ASSEMBLY__ */
#endif /* _LINUX_MMZONE_H */
