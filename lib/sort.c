/*
 * A fast, small, non-recursive O(nlog n) sort for the Linux kernel
 *
 * Jan 23 2005  Matt Mackall <mpm@selenic.com>
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/sort.h>

/*********************************************************************************************************
** 函数名称: u32_swap
** 功能描述: 交换指定的两个 uint32 类型变量值
** 输	 入: a - 指定的第一个 uint32 类型变量指针
**         : b - 指定的第二个 uint32 类型变量指针
**         : size - 每个 uint32 类型变量占的字节数
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void u32_swap(void *a, void *b, int size)
{
	u32 t = *(u32 *)a;
	*(u32 *)a = *(u32 *)b;
	*(u32 *)b = t;
}

/*********************************************************************************************************
** 函数名称: generic_swap
** 功能描述: 交换指定长度的两个变量值
** 输	 入: a - 指定的第一个变量指针
**         : b - 指定的第二个变量指针
**         : size - 指定的变量长度字节数
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static void generic_swap(void *a, void *b, int size)
{
	char t;

	do {
		t = *(char *)a;
		*(char *)a++ = *(char *)b;
		*(char *)b++ = t;
	} while (--size > 0);
}

/**
 * sort - sort an array of elements
 * @base: pointer to data to sort
 * @num: number of elements
 * @size: size of each element
 * @cmp_func: pointer to comparison function
 * @swap_func: pointer to swap function or NULL
 *
 * This function does a heapsort on the given array. You may provide a
 * swap_func function optimized to your element type.
 *
 * Sorting time is O(n log n) both on average and worst-case. While
 * qsort is about 20% faster on average, it suffers from exploitable
 * O(n*n) worst-case behavior and extra memory requirements that make
 * it less suitable for kernel use.
 */
/*********************************************************************************************************
** 函数名称: sort
** 功能描述: 根据函数指定的参数对指定的数据进行排序操作
** 输	 入: base - 指定的数据地址
**         : num - 指定的数据元素个数
**         : size - 每个数据元素占的字节数
**         : cmp_func - 指定的比较单个数据元素函数指针
**         : swap_func - 指定的交换单元数据元素函数指针
** 输	 出: 
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
void sort(void *base, size_t num, size_t size,
	  int (*cmp_func)(const void *, const void *),
	  void (*swap_func)(void *, void *, int size))
{
	/* pre-scale counters for performance */
	int i = (num/2 - 1) * size, n = num * size, c, r;

	if (!swap_func)
		swap_func = (size == 4 ? u32_swap : generic_swap);

	/* heapify */
	for ( ; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r  = c) {
			c = r * 2 + size;
			if (c < n - size &&
					cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i > 0; i -= size) {
		swap_func(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size &&
					cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}
}

EXPORT_SYMBOL(sort);

#if 0
#include <linux/slab.h>
/* a simple boot-time regression test */

int cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int sort_test(void)
{
	int *a, i, r = 1;

	a = kmalloc(1000 * sizeof(int), GFP_KERNEL);
	BUG_ON(!a);

	printk("testing sort()\n");

	for (i = 0; i < 1000; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, 1000, sizeof(int), cmpint, NULL);

	for (i = 0; i < 999; i++)
		if (a[i] > a[i+1]) {
			printk("sort() failed!\n");
			break;
		}

	kfree(a);

	return 0;
}

module_init(sort_test);
#endif
