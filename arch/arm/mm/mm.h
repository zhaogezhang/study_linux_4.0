#ifdef CONFIG_MMU
#include <linux/list.h>
#include <linux/vmalloc.h>

#include <asm/pgtable.h>

/* the upper-most page table pointer */
extern pmd_t *top_pmd;

/*
 * 0xffff8000 to 0xffffffff is reserved for any ARM architecture
 * specific hacks for copying pages efficiently, while 0xffff4000
 * is reserved for VIPT aliasing flushing by generic code.
 *
 * Note that we don't allow VIPT aliasing caches with SMP.
 */
#define COPYPAGE_MINICACHE	0xffff8000
#define COPYPAGE_V6_FROM	0xffff8000
#define COPYPAGE_V6_TO		0xffffc000
/* PFN alias flushing, for VIPT caches */
#define FLUSH_ALIAS_START	0xffff4000

static inline void set_top_pte(unsigned long va, pte_t pte)
{
	pte_t *ptep = pte_offset_kernel(top_pmd, va);
	set_pte_ext(ptep, pte, 0);
	local_flush_tlb_kernel_page(va);
}

static inline pte_t get_top_pte(unsigned long va)
{
	pte_t *ptep = pte_offset_kernel(top_pmd, va);
	return *ptep;
}

static inline pmd_t *pmd_off_k(unsigned long virt)
{
	return pmd_offset(pud_offset(pgd_offset_k(virt), virt), virt);
}

struct mem_type {
	pteval_t prot_pte;     // 页表 pte 属性
 
	// The stage-2 memory attributes are distinct from the Hyp memory
	// attributes and the Stage-1 memory attributes.  We were using the stage-1
	// memory attributes for stage-2 mappings causing device mappings to be
	// mapped as normal memory.  Add the S2 equivalent defines for memory
	// attributes and fix the comments explaining the defines while at it.
	
	// Add a prot_pte_s2 field to the mem_type struct and fill out the field
	// for device mappings accordingly.
	// details at : https://patchwork.kernel.org/patch/3433171/
	pteval_t prot_pte_s2;  // The stage-2 memory attributes for device mappings accordingly
	
	pmdval_t prot_l1;      // 页表中 pmd 成员属性（二级页表中的 pgd）
	pmdval_t prot_sect;    // section（段）映射时的页表属性
	unsigned int domain;   // 内存“域”标记变量
};

const struct mem_type *get_mem_type(unsigned int type);

extern void __flush_dcache_page(struct address_space *mapping, struct page *page);

/*
 * ARM specific vm_struct->flags bits.
 */

/* (super)section-mapped I/O regions used by ioremap()/iounmap() */
#define VM_ARM_SECTION_MAPPING	0x80000000

/* permanent static mappings from iotable_init() */
#define VM_ARM_STATIC_MAPPING	0x40000000

/* empty mapping */
#define VM_ARM_EMPTY_MAPPING	0x20000000

/* mapping type (attributes) for permanent static mappings */
#define VM_ARM_MTYPE(mt)		((mt) << 20)
#define VM_ARM_MTYPE_MASK	(0x1f << 20)

/* consistent regions used by dma_alloc_attrs() */
#define VM_ARM_DMA_CONSISTENT	0x20000000


struct static_vm {
	struct vm_struct vm;
	struct list_head list;
};

extern struct list_head static_vmlist;
extern struct static_vm *find_static_vm_vaddr(void *vaddr);
extern __init void add_static_vm_early(struct static_vm *svm);

#endif

#ifdef CONFIG_ZONE_DMA
extern phys_addr_t arm_dma_limit;
extern unsigned long arm_dma_pfn_limit;
#else
#define arm_dma_limit ((phys_addr_t)~0)
#define arm_dma_pfn_limit (~0ul >> PAGE_SHIFT)
#endif

extern phys_addr_t arm_lowmem_limit;

void __init bootmem_init(void);
void arm_mm_memblock_reserve(void);
void dma_contiguous_remap(void);

unsigned long __clear_cr(unsigned long mask);
