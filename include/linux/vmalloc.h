#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/list.h>
#include <asm/page.h>		/* pgprot_t */
#include <linux/rbtree.h>

struct vm_area_struct;		/* vma defining user mapping in mm_types.h */

/* bits in flags of vmalloc's vm_struct below */
#define VM_IOREMAP		0x00000001	/* ioremap() and friends */
#define VM_ALLOC		0x00000002	/* vmalloc() */
#define VM_MAP			0x00000004	/* vmap()ed pages */
#define VM_USERMAP		0x00000008	/* suitable for remap_vmalloc_range */
#define VM_VPAGES		0x00000010	/* buffer for pages was vmalloc'ed */
#define VM_UNINITIALIZED	0x00000020	/* vm_struct is not fully initialized */
#define VM_NO_GUARD		0x00000040      /* don't add guard page */
#define VM_KASAN		0x00000080      /* has allocated kasan shadow memory */
/* bits [20..32] reserved for arch specific ioremap internals */

/*
 * Maximum alignment for ioremap() regions.
 * Can be overriden by arch-specific value.
 */
#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/* 128 pages */
#endif

// 用来映射内核空间中非连续空间的数据结构
// 我们以 3:1 的分配为例，说明内核空间地址分配方式：
// 1. 3G ~ 3G + 896M 为内核直接映射区域，所谓的直接映射，就是这部分虚拟地址在映射的时候
//    要满足一下两个条件：
//    a. 虚拟地址和物理地址之间存在一个固定偏移量
//    b. 虚拟地址和物理地址都是连续的
// 2. 3G + 896M ~ 3G + 896M + 8M 为安全保护区，对这个地址空间的所有访问被视为非法访问
// 3. 3G + 896M + 8M ~ 4G 为高端内存区，这是一个动态映射的区域，此区域的虚拟内存空间
//    使用情况是通过 struct vm_struct 数据结构来维护的

// 描述了一个 vm 地址空间以及和其对应的物理页相关信息
struct vm_struct {
	struct vm_struct	*next;    // 指向下一个 vm 结构体
	void			*addr;        // 虚拟地址起始地址
	unsigned long		size;     // 从 addr 开始，有效的虚拟地址空间大小
	unsigned long		flags;    // 内存属性标记
	struct page		**pages;      // 物理页描述符数组指针
	unsigned int		nr_pages; // 包含的物理页个数
	phys_addr_t		phys_addr;    // 用来映射硬件设备的 IO 共享内存，其他情况下为 0
	const void		*caller;      // 创建映射关系的函数（模块）
};

// 描述一个纯粹的 vm 地址空间
struct vmap_area {
	// 当前 vmap_area 所代表的虚拟地址空间的起始地址
	unsigned long va_start;

	// 当前 vmap_area 所代表的虚拟地址空间的结束地址
	unsigned long va_end;
	
	unsigned long flags;

	// 系统会把所有已经分配的 vmap_area 按照地址大小插入到 vmap_area_root 全局红黑树中
	// 这个红黑树主要是为了快速查找指定虚拟地址所对应的 vmap_area 结构，参考 alloc_vmap_area 函数
	struct rb_node rb_node;         /* address sorted rbtree */

	// 系统会把所有已经分配的 vmap_area 按照地址大小插入到 vmap_area_list 全局链表中
	// 这个链表主要是在以指定的虚拟地址开始，向后依次查找并分配我们需要的虚拟地址空间
	// 块的时候使用，参考 alloc_vmap_area 函数
	struct list_head list;          /* address sorted list */
	
	struct list_head purge_list;    /* "lazy purge" list */
	struct vm_struct *vm;
	struct rcu_head rcu_head;
};

/*
 *	Highlevel APIs for driver use
 */
extern void vm_unmap_ram(const void *mem, unsigned int count);
extern void *vm_map_ram(struct page **pages, unsigned int count,
				int node, pgprot_t prot);
extern void vm_unmap_aliases(void);

#ifdef CONFIG_MMU
extern void __init vmalloc_init(void);
#else
static inline void vmalloc_init(void)
{
}
#endif

extern void *vmalloc(unsigned long size);
extern void *vzalloc(unsigned long size);
extern void *vmalloc_user(unsigned long size);
extern void *vmalloc_node(unsigned long size, int node);
extern void *vzalloc_node(unsigned long size, int node);
extern void *vmalloc_exec(unsigned long size);
extern void *vmalloc_32(unsigned long size);
extern void *vmalloc_32_user(unsigned long size);
extern void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot);
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller);

extern void vfree(const void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
extern void vunmap(const void *addr);

extern int remap_vmalloc_range_partial(struct vm_area_struct *vma,
				       unsigned long uaddr, void *kaddr,
				       unsigned long size);

extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);
void vmalloc_sync_all(void);
 
/*
 *	Lowlevel-APIs (not for driver use!)
 */

// 获取指定的 vm_struct 所表示的虚拟地址空间块大小
static inline size_t get_vm_area_size(const struct vm_struct *area)
{
	if (!(area->flags & VM_NO_GUARD))
		/* return actual size without guard page */
		return area->size - PAGE_SIZE;
	else
		return area->size;

}

extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);
extern struct vm_struct *get_vm_area_caller(unsigned long size,
					unsigned long flags, const void *caller);
extern struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
					unsigned long start, unsigned long end);
extern struct vm_struct *__get_vm_area_caller(unsigned long size,
					unsigned long flags,
					unsigned long start, unsigned long end,
					const void *caller);
extern struct vm_struct *remove_vm_area(const void *addr);
extern struct vm_struct *find_vm_area(const void *addr);

extern int map_vm_area(struct vm_struct *area, pgprot_t prot,
			struct page **pages);
#ifdef CONFIG_MMU
extern int map_kernel_range_noflush(unsigned long start, unsigned long size,
				    pgprot_t prot, struct page **pages);
extern void unmap_kernel_range_noflush(unsigned long addr, unsigned long size);
extern void unmap_kernel_range(unsigned long addr, unsigned long size);
#else
static inline int
map_kernel_range_noflush(unsigned long start, unsigned long size,
			pgprot_t prot, struct page **pages)
{
	return size >> PAGE_SHIFT;
}
static inline void
unmap_kernel_range_noflush(unsigned long addr, unsigned long size)
{
}
static inline void
unmap_kernel_range(unsigned long addr, unsigned long size)
{
}
#endif

/* Allocate/destroy a 'vmalloc' VM area. */
extern struct vm_struct *alloc_vm_area(size_t size, pte_t **ptes);
extern void free_vm_area(struct vm_struct *area);

/* for /dev/kmem */
extern long vread(char *buf, char *addr, unsigned long count);
extern long vwrite(char *buf, char *addr, unsigned long count);

/*
 *	Internals.  Dont't use..
 */
// 将 vmap_area 表示的地址从小到大排序
extern struct list_head vmap_area_list;

extern __init void vm_area_add_early(struct vm_struct *vm);
extern __init void vm_area_register_early(struct vm_struct *vm, size_t align);

#ifdef CONFIG_SMP
# ifdef CONFIG_MMU
struct vm_struct **pcpu_get_vm_areas(const unsigned long *offsets,
				     const size_t *sizes, int nr_vms,
				     size_t align);

void pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms);
# else
static inline struct vm_struct **
pcpu_get_vm_areas(const unsigned long *offsets,
		const size_t *sizes, int nr_vms,
		size_t align)
{
	return NULL;
}

static inline void
pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms)
{
}
# endif
#endif

struct vmalloc_info {
	unsigned long   used;
	unsigned long   largest_chunk;
};

#ifdef CONFIG_MMU
#define VMALLOC_TOTAL (VMALLOC_END - VMALLOC_START)
extern void get_vmalloc_info(struct vmalloc_info *vmi);
#else

#define VMALLOC_TOTAL 0UL
#define get_vmalloc_info(vmi)			\
do {						\
	(vmi)->used = 0;			\
	(vmi)->largest_chunk = 0;		\
} while (0)
#endif

#endif /* _LINUX_VMALLOC_H */
