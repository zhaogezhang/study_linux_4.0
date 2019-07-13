#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/auxvec.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/uprobes.h>
#include <linux/page-flags-layout.h>
#include <asm/page.h>
#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;
struct mem_cgroup;

#define USE_SPLIT_PTE_PTLOCKS	(NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS)
#define USE_SPLIT_PMD_PTLOCKS	(USE_SPLIT_PTE_PTLOCKS && \
		IS_ENABLED(CONFIG_ARCH_ENABLE_SPLIT_PMD_PTLOCK))
#define ALLOC_SPLIT_PTLOCKS	(SPINLOCK_SIZE > BITS_PER_LONG/8)

typedef void compound_page_dtor(struct page *);

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 *
 * The objects in struct page are organized in double word blocks in
 * order to allows us to use atomic double word operations on portions
 * of struct page. That is currently only used by slub but the arrangement
 * allows the use of atomic double word operations on the flags/mapping
 * and lru list pointers also.
 */
struct page {
	/* First double word block */

    /* 用于页描述符，一组标志（如 PG_locked、PG_error），同时页框所在的管理区和 node 的编号也保存在当中 */
    
    /* 在lru算法中主要用到的标志
     * PG_active: 表示此页当前是否活跃，当放到或者准备放到活动 lru 链表时，被置位
     * PG_referenced: 表示此页最近是否被访问，每次页面访问都会被置位
     * PG_lru: 表示此页是处于 lru 链表中的
     * PG_mlocked: 表示此页被 mlock() 锁在内存中，禁止换出和释放
     * PG_swapbacked: 表示此页依靠 swap，可能是进程的匿名页(堆、栈、数据段)，匿名 mmap 共享内存映射，shmem 共享内存映射
     */
	unsigned long flags;		/* Atomic flags, some possibly
					             * updated asynchronously */
								 	
	union {
		// 在不同场景下，mapping 指向不同类型变量，我们是通过这个变量的低位判断的
		// 详细的描述请参考 PAGE_MAPPING_ANON
		
        /* 
         * 最低两位用于判断类型，其他位数用于保存指向的地址
         * 如果为空，则该页属于交换高速缓存（swap cache，swap 时会产生竞争条件，用 swap cache 解决）  
         * 不为空，如果最低位为 1，该页为匿名页，指向对应的 anon_vma （分配时需要对齐）
         * 不为空，如果最低位为 0，则该页为文件页，指向文件的 address_space
         */
		struct address_space *mapping;	/* If low bit clear, points to
						                 * inode address_space, or NULL.
						                 * If page mapped as anonymous
						                 * memory, low bit is set, and
						                 * it points to anon_vma object:
						                 * see PAGE_MAPPING_ANON below.
						                 */
						                 
		void *s_mem;			/* slab first object */
	};

	/* Second double word */
	struct {
		union {
			// 表示当前物理内存页按照物理页框为单位在物理内存空间中的偏移量
			pgoff_t index;		/* Our offset within mapping. */

			// 在 sl[aou]b 内存分配算法中，一个内存页就是一个 slab，其中包含很对个 object 
			// 我们用 freelist 表示在这个内存页中第一个 free object 成员的地址，而在 slub 中
			// 所有的 slab 对象中都有一个指向下一个 slab 对象的指针，形成了一个单向链表
			// 当我们从一个 slab 中申请一个新的 object 的时候，freelist 会相应的向后移动
			// 这个值是和 kmem_cache_cpu->freelist 区别是：
			// 他们都指向了 slab 的第一个 free object，但是使用时机不一样，如果这个 slab 是
			// per cpu slab，则通过 kmem_cache_cpu->freelist 追踪记录 slab 的第一个 free object
			// 如果这个 slab 不在 per cpu 上，则通过 page->freelist 追踪记录 slab 的第一个 free object
			void *freelist;		/* sl[aou]b first free object */
			
			bool pfmemalloc;	/* If set by the page allocator,
						 * ALLOC_NO_WATERMARKS was set
						 * and the low watermark was not
						 * met implying that the system
						 * is under some pressure. The
						 * caller should try ensure
						 * this page is only used to
						 * free other pages.
						 */
		};

		union {
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
	defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
			/* Used for cmpxchg_double in slub */
			unsigned long counters;
#else
			/*
			 * Keep _count separate from slub cmpxchg_double data.
			 * As the rest of the double word is protected by
			 * slab_lock but _count is not.
			 */
			unsigned counters;
#endif

			struct {

				union {
					/*
					 * Count of ptes mapped in
					 * mms, to show when page is
					 * mapped & limit reverse map
					 * searches.
					 *
					 * Used also for tail pages
					 * refcounting instead of
					 * _count. Tail pages cannot
					 * be mapped and keeping the
					 * tail page _count zero at
					 * all times guarantees
					 * get_page_unless_zero() will
					 * never succeed on tail
					 * pages.
					 */
					atomic_t _mapcount;

					struct { /* SLUB */
						// 表示当前 slab 中已经被使用（分配）的 object 个数
						unsigned inuse:16;

						// 表示当前 slab 中一共有的 object 个数（分配的和空闲的）
						unsigned objects:15;

						// frozen 代表 slab 在 per_cpu_slub
						// unfroze 代表在 partial 队列或者 full 队列
						unsigned frozen:1;
					};
					int units;	/* SLOB */
				};
				atomic_t _count;		/* Usage count, see below. */
			};
			unsigned int active;	/* SLAB */
		};
	};

	/* Third double word block */
	union {
		/* 页处于不同情况时，加入的链表不同
         * 1.是一个进程正在使用的页，加入到对应 lru 链表和 lru 缓存中
         * 2.如果为空闲页框，并且是空闲块的第一个页，加入到伙伴系统的空闲块链表中(只有空闲块的第一个页需要加入)
         * 3.如果是一个 slab 的第一个页，则将其加入到 slab 链表中(比如 slab 的满 slab 链表，slub 的部分空 slab 链表)
         * 4.将页隔离时用于加入隔离链表
         */
		struct list_head lru;	/* Pageout list, eg. active_list
					             * protected by zone->lru_lock !
					             * Can be used as a generic list
					             * by the page owner.
					             */
					             
		struct {		/* slub per cpu partial pages */
			struct page *next;	/* Next partial slab */
#ifdef CONFIG_64BIT
			int pages;	/* Nr of partial slabs left */
			int pobjects;	/* Approximate # of objects */
#else
			short int pages;
			short int pobjects;
#endif
		};

		struct slab *slab_page; /* slab fields */
		struct rcu_head rcu_head;	/* Used by SLAB
						 * when destroying via RCU
						 */
		/* First tail page of compound page */
		struct {
			compound_page_dtor *compound_dtor;
			unsigned long compound_order;
		};

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && USE_SPLIT_PMD_PTLOCKS
		pgtable_t pmd_huge_pte; /* protected by page->ptl */
#endif
	};

	/* Remainder is not double word aligned */
	union {
		unsigned long private;		/* Mapping-private opaque data:
					 	             * usually used for buffer_heads
						             * if PagePrivate set; used for
						             * swp_entry_t if PageSwapCache;
						             * indicates order in the buddy
						             * system if PG_buddy is set.
						             */
#if USE_SPLIT_PTE_PTLOCKS
#if ALLOC_SPLIT_PTLOCKS
		spinlock_t *ptl;
#else
		spinlock_t ptl;
#endif
#endif
		struct kmem_cache *slab_cache;	/* SL[AU]B: Pointer to slab */
		struct page *first_page;	/* Compound tail pages */
	};

#ifdef CONFIG_MEMCG
	struct mem_cgroup *mem_cgroup;
#endif

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef CONFIG_KMEMCHECK
	/*
	 * kmemcheck wants to track the status of each byte in a page; this
	 * is a pointer to such a status block. NULL if not tracked.
	 */
	void *shadow;
#endif

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
}
/*
 * The struct page can be forced to be double word aligned so that atomic ops
 * on double words work. The SLUB allocator can make use of such a feature.
 */
#ifdef CONFIG_HAVE_ALIGNED_STRUCT_PAGE
	__aligned(2 * sizeof(unsigned long))
#endif
;

struct page_frag {
	struct page *page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	__u32 offset;
	__u32 size;
#else
	__u16 offset;
	__u16 size;
#endif
};

typedef unsigned long __nocast vm_flags_t;

/*
 * A region containing a mapping of a non-memory backed file under NOMMU
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	vm_flags_t	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	int		vm_usage;	/* region usage count (access under nommu_region_sem) */
	bool		vm_icache_flushed : 1; /* true if the icache has been flushed for
						* this region */
};

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
// 表示进程地址空间内的一个虚拟地址块，内核地址空间的虚拟地址块是通过
// vm_struct 结构表示的
struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */

	// 当亲 vma 表示的地址块的起始地址和结束地址值，即当前 vma 表示的地址范围
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					               within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next, *vm_prev;

	struct rb_node vm_rb;

	/*
	 * Largest free memory gap in bytes to the left of this VMA.
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
	 */
	// 在整个 vma 线性地址空间中，记录在当前 vma 地址空间之前，最大的、空闲地址
	// 空间块所代表的地址大小，在分配空闲 vma 的时候会使用到
	unsigned long rb_subtree_gap;

	/* Second cache line starts here. */

	// 指向当前 vma 所属进程的内存空间描述符
	struct mm_struct *vm_mm;	/* The address space we belong to. */

	// 页表项标志的初值，当增加一个页时，内核根据这个字段的值设置相应页表项中的标志
	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
	
	unsigned long vm_flags;		/* Flags, see mm.h. 传送门 - VM_NONE */

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap interval tree.
	 */
	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	// 这个变量会和 struct anon_vma_chain 结构体中的 same_vma 变量形成一个链表
	struct list_head anon_vma_chain; /* Serialized by mmap_sem &
					                  * page_table_lock */

	// anon_vma 变量会指向和当前 vma 相关的 struct anon_vma 结构
	struct anon_vma *anon_vma;	     /* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	// 如果当前 vma 是文件映射，用这个字段表示当前 vma 的 vma->vm_start 在所映
	// 射的文件内的偏移量，这个偏移量是以物理内存页大小为单位的。如果当前 vma 
	// 是匿名映射，则用这个字段表示当前 vma 的 vma->vm_start 按照物理内存页为
	// 单位在整个物理内存空间中所对应的偏移量
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					               units, *not* PAGE_CACHE_SIZE */

	// 如果当前 vma 是文件映射中的一部分，这个成员存储映射的文件结构的地址
	struct file * vm_file;		/* File we map to (can be NULL). */
					  
	void * vm_private_data;		/* was vm_pte (shared mem) */

#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
};

struct core_thread {
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state {
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

enum {
	MM_FILEPAGES,
	MM_ANONPAGES,
	MM_SWAPENTS,
	NR_MM_COUNTERS
};

#if USE_SPLIT_PTE_PTLOCKS && defined(CONFIG_MMU)
#define SPLIT_RSS_COUNTING
/* per-thread cached information, */
struct task_rss_stat {
	int events;	/* for synchronization threshold */
	int count[NR_MM_COUNTERS];
};
#endif /* USE_SPLIT_PTE_PTLOCKS */

struct mm_rss_stat {
	atomic_long_t count[NR_MM_COUNTERS];
};

struct kioctx_table;
struct mm_struct {
	// 当前进程地址空间中，按地址大小顺序排序的链表头
	struct vm_area_struct *mmap;		/* list of VMAs */

	// 当前进程地址空间中，以地址为键值进行排列的红黑树根节点
	struct rb_root mm_rb;

	// 这个值和 struct task_struct 结构中的 vmacache_seqnum 相对应
	// 只有当这两个值相等的时候，才表示 vmacache 有效，所以我们如果
	// 想要 invalid 当前 vmacache，只需要把这个值加一即可
	u32 vmacache_seqnum;                   /* per-thread vmacache */
	
#ifdef CONFIG_MMU
	unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
#endif
	unsigned long mmap_base;		/* base of mmap area */
	unsigned long mmap_legacy_base;         /* base of mmap area in bottom-up allocations */
	unsigned long task_size;		/* size of task vm space */

	// 记录当前进程地址空间中，最大的有效虚拟地址值
	unsigned long highest_vm_end;		/* highest vma end address */

	// 记录当前进程的 pgd（页全局目录）数据指针
	pgd_t * pgd;
	
	atomic_t mm_users;			/* How many users with user space? */
	atomic_t mm_count;			/* How many references to "struct mm_struct" (users count as 1) */
	atomic_long_t nr_ptes;		/* PTE page table pages */
	atomic_long_t nr_pmds;		/* PMD page table pages */

	// 在当前进程地址空间中包含的 vma 个数，在每次插入 vma 的时候都加一
	int map_count;				/* number of VMAs */

	spinlock_t page_table_lock;		/* Protects page tables and some counters */
	struct rw_semaphore mmap_sem;

	struct list_head mmlist;		/* List of maybe swapped mm's.	These are globally strung
						             * together off init_mm.mmlist, and are protected
						             * by mmlist_lock
						             */

	unsigned long hiwater_rss;	/* High-watermark of RSS usage */
	unsigned long hiwater_vm;	/* High-water virtual memory usage */

	// 当前进程映射的所有 vma 地址空间大小所占用的内存页数量
	unsigned long total_vm;		/* Total pages mapped */

	// 当亲进程映射的 vma 地址空间中，被锁住不能被换出的内存页数量
	unsigned long locked_vm;	/* Pages that have PG_mlocked set */
	
	unsigned long pinned_vm;	/* Refcount permanently increased */

	// 当亲进程映射的 vma 地址空间中，用作共享文件映射的内存页数量
	unsigned long shared_vm;	/* Shared pages (files) */

	// 当亲进程映射的 vma 地址空间中，用作可执行文件映射的内存页数量
	unsigned long exec_vm;		/* VM_EXEC & ~VM_WRITE */

	// 当亲进程映射的 vma 地址空间中，用作用户进程堆栈映射的内存页数量
	unsigned long stack_vm;		/* VM_GROWSUP/DOWN */
	
	unsigned long def_flags;

	// 一个进程的地址空间分配结构如下：
	//    4G   	 -------------------
	//
	//               内核地址空间
	//
	//    3G   	 ------------------- start_stack
	//               满递减用户栈
	//      	 -------------------    
	//      
	//         	     mmap分配空间
	//
	//    1G   	 -------------------
	//
	//                  堆空间
	//
	// start brk ------------------- end_data/start_brk
	//                  数据段
	//           ------------------- end_code/start_data
	//                  代码段
	//      	 ------------------- start_code
	//                  保留区
	//     0   	 -------------------
	//
	// 分别记录当前用户进程代码段起始地址、代码段结束地址、数据段起始地址和数据段结束地址
	unsigned long start_code, end_code, start_data, end_data;

	// 分别记录当前用户进程 brk 分区的起始地址、动态分配区的当前底部和用户进程栈起始地址
	unsigned long start_brk, brk, start_stack;

	// 分别记录当前用户进程启动参数起始地址，启动参数结束地址、环境变量起始地址和环境变量结束地址
	unsigned long arg_start, arg_end, env_start, env_end;

	unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

	/*
	 * Special counters, in some configurations protected by the
	 * page_table_lock, in other configurations by being atomic.
	 */
	struct mm_rss_stat rss_stat;

	struct linux_binfmt *binfmt;

	cpumask_var_t cpu_vm_mask_var;

	/* Architecture-specific MM context */
	mm_context_t context;

	unsigned long flags; /* Must use atomic bitops to access the bits */

	struct core_state *core_state; /* coredumping support */
#ifdef CONFIG_AIO
	spinlock_t			ioctx_lock;
	struct kioctx_table __rcu	*ioctx_table;
#endif
#ifdef CONFIG_MEMCG
	/*
	 * "owner" points to a task that is regarded as the canonical
	 * user/owner of this mm. All of the following must be true in
	 * order for it to be changed:
	 *
	 * current == mm->owner
	 * current->mm != mm
	 * new_owner->mm == mm
	 * new_owner->alloc_lock is held
	 */
	struct task_struct __rcu *owner;
#endif

	/* store ref to file /proc/<pid>/exe symlink points to */
	// 当前进程的可执行文件对应的文件结构指针
	struct file *exe_file;

#ifdef CONFIG_MMU_NOTIFIER
	struct mmu_notifier_mm *mmu_notifier_mm;
#endif
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	pgtable_t pmd_huge_pte; /* protected by page_table_lock */
#endif
#ifdef CONFIG_CPUMASK_OFFSTACK
	struct cpumask cpumask_allocation;
#endif
#ifdef CONFIG_NUMA_BALANCING
	/*
	 * numa_next_scan is the next time that the PTEs will be marked
	 * pte_numa. NUMA hinting faults will gather statistics and migrate
	 * pages to new nodes if necessary.
	 */
	unsigned long numa_next_scan;

	/* Restart point for scanning and setting pte_numa */
	unsigned long numa_scan_offset;

	/* numa_scan_seq prevents two threads setting pte_numa */
	int numa_scan_seq;
#endif
#if defined(CONFIG_NUMA_BALANCING) || defined(CONFIG_COMPACTION)
	/*
	 * An operation with batched TLB flushing is going on. Anything that
	 * can move process memory needs to flush the TLB when moving a
	 * PROT_NONE or PROT_NUMA mapped page.
	 */
	bool tlb_flush_pending;
#endif
	struct uprobes_state uprobes_state;
#ifdef CONFIG_X86_INTEL_MPX
	/* address of the bounds directory */
	void __user *bd_addr;
#endif
};

static inline void mm_init_cpumask(struct mm_struct *mm)
{
#ifdef CONFIG_CPUMASK_OFFSTACK
	mm->cpu_vm_mask_var = &mm->cpumask_allocation;
#endif
	cpumask_clear(mm->cpu_vm_mask_var);
}

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return mm->cpu_vm_mask_var;
}

#if defined(CONFIG_NUMA_BALANCING) || defined(CONFIG_COMPACTION)
/*
 * Memory barriers to keep this state in sync are graciously provided by
 * the page table locks, outside of which no page table modifications happen.
 * The barriers below prevent the compiler from re-ordering the instructions
 * around the memory barriers that are already present in the code.
 */
static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	barrier();
	return mm->tlb_flush_pending;
}
static inline void set_tlb_flush_pending(struct mm_struct *mm)
{
	mm->tlb_flush_pending = true;

	/*
	 * Guarantee that the tlb_flush_pending store does not leak into the
	 * critical section updating the page tables
	 */
	smp_mb__before_spinlock();
}
/* Clearing is done after a TLB flush, which also provides a barrier. */
static inline void clear_tlb_flush_pending(struct mm_struct *mm)
{
	barrier();
	mm->tlb_flush_pending = false;
}
#else
static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	return false;
}
static inline void set_tlb_flush_pending(struct mm_struct *mm)
{
}
static inline void clear_tlb_flush_pending(struct mm_struct *mm)
{
}
#endif

struct vm_special_mapping
{
	const char *name;
	struct page **pages;
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	NR_TLB_FLUSH_REASONS,
};

 /*
  * A swap entry has to fit into a "unsigned long", as the entry is hidden
  * in the "index" field of the swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

#endif /* _LINUX_MM_TYPES_H */
