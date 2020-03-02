#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H

#include <linux/sched.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/threads.h>
#include <linux/nsproxy.h>
#include <linux/kref.h>
#include <linux/ns_common.h>

/* 表示当前系统内和系统 pid 对应的位图变量结构 */
struct pidmap {
       /* 表示当前 pidmap 中空闲的 bit 位数 */
       atomic_t nr_free;

	   /* 用来存储当前 pidmap 中的 bit 数据 */
       void *page;
};

/* 表示一个物理内存页包含的 bits 数，例如 0x8000 */
#define BITS_PER_PAGE		(PAGE_SIZE * 8)

/* 表示一个物理内存页包含的 bits 数的掩码值，例如 0x7FFF */
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)

/* 表示容纳当前系统在一个 pid namespace 中所有 pid 的位图空间大小，单位是物理内存页 */
#define PIDMAP_ENTRIES		((PID_MAX_LIMIT+BITS_PER_PAGE-1)/BITS_PER_PAGE)

struct fs_pin;

/* 在 linux 系统中 pid namespace 是通过一个树形结构组织起来的 */
struct pid_namespace {
    /* 表示当前 pid namespace 对象的引用计数 */
	struct kref kref;

	/* 为当前系统的每一个 pid 分配一个 bit */
	struct pidmap pidmap[PIDMAP_ENTRIES];
	
	struct rcu_head rcu;

    /* 表示最后一次申请的 pid 在当前 pid namespace 中的偏移量 */
	int last_pid;

	unsigned int nr_hashed;
	struct task_struct *child_reaper;

	/* 为当前 pid namespace 分配的 kmem_cache 内存分配器指针 */
	struct kmem_cache *pid_cachep;

    /* 表示当前 pid namespace 的层级，即在系统 pid namespace 树形结构中的深度 */
	unsigned int level;
	
	struct pid_namespace *parent;
#ifdef CONFIG_PROC_FS
	struct vfsmount *proc_mnt;
	struct dentry *proc_self;
	struct dentry *proc_thread_self;
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	struct fs_pin *bacct;
#endif
	struct user_namespace *user_ns;
	struct work_struct proc_work;
	kgid_t pid_gid;
	int hide_pid;
	int reboot;	/* group exit code if this pidns was rebooted */
	struct ns_common ns;
};

extern struct pid_namespace init_pid_ns;

#define PIDNS_HASH_ADDING (1U << 31)

#ifdef CONFIG_PID_NS
/*********************************************************************************************************
** 函数名称: get_pid_ns
** 功能描述: 增加指定的 pid namespace 的引用计数值
** 输	 入: ns - 指定的 pid namespace 指针
** 输	 出: ns - 引用的 pid namespace 指针
** 全局变量: 
** 调用模块: 
*********************************************************************************************************/
static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	if (ns != &init_pid_ns)
		kref_get(&ns->kref);
	return ns;
}

extern struct pid_namespace *copy_pid_ns(unsigned long flags,
	struct user_namespace *user_ns, struct pid_namespace *ns);
extern void zap_pid_ns_processes(struct pid_namespace *pid_ns);
extern int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd);
extern void put_pid_ns(struct pid_namespace *ns);

#else /* !CONFIG_PID_NS */
#include <linux/err.h>

static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	return ns;
}

static inline struct pid_namespace *copy_pid_ns(unsigned long flags,
	struct user_namespace *user_ns, struct pid_namespace *ns)
{
	if (flags & CLONE_NEWPID)
		ns = ERR_PTR(-EINVAL);
	return ns;
}

static inline void put_pid_ns(struct pid_namespace *ns)
{
}

static inline void zap_pid_ns_processes(struct pid_namespace *ns)
{
	BUG();
}

static inline int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd)
{
	return 0;
}
#endif /* CONFIG_PID_NS */

extern struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);
void pidhash_init(void);
void pidmap_init(void);

#endif /* _LINUX_PID_NS_H */
