/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 */

#ifndef __VMAP_STACK_H__
#define __VMAP_STACK_H__

#include <linux/seq_file.h>

#define STACK_SHRINK_THRESHOLD		(PAGE_SIZE + 1024)
#define STACK_SHRINK_SLEEP		(HZ)
#ifdef CONFIG_64BIT
#define VM_STACK_AREA_SIZE		SZ_512M
#define VMAP_ADDR_START			VMALLOC_START
#define VMAP_ADDR_END			VMALLOC_END
#define VMAP_ALIGN			VM_STACK_AREA_SIZE
#else
/* currently support max 6144 tasks on 32bit */
#define VM_STACK_AREA_SIZE		(SZ_64M - SZ_16M)
#ifdef CONFIG_KASAN		/* change place if open kasan */
#define VMAP_ADDR_START			VMALLOC_START
#define VMAP_ADDR_END			(VMALLOC_START + VM_STACK_AREA_SIZE)
#else
#define VMAP_ADDR_START			MODULES_VADDR
#define VMAP_ADDR_END			MODULES_END
#endif /* CONFIG_KASAN */
#define VMAP_ALIGN			SZ_64M
#endif

#define STACK_TOP_PAGE_OFF		(THREAD_SIZE - PAGE_SIZE)

#define MAX_TASKS			(VM_STACK_AREA_SIZE / THREAD_SIZE)

#define VMAP_PAGE_FLAG			(__GFP_ZERO | __GFP_HIGH |\
					 __GFP_ATOMIC | __GFP_REPEAT)

#ifdef CONFIG_KASAN
#define VMAP_CACHE_PAGE_ORDER		9
#else
#define VMAP_CACHE_PAGE_ORDER		9
#endif
#define VMAP_CACHE_PAGE			BIT(VMAP_CACHE_PAGE_ORDER)
#define CACHE_MAINTAIN_DELAY		(HZ / 2)

struct aml_vmap {
	spinlock_t vmap_lock;		/* for address space */
	unsigned int start_bit;
	int cached_pages;
	struct vm_struct *root_vm;
	unsigned long *bitmap;
	struct list_head list;
	spinlock_t page_lock;		/* for cached pages */
};

#ifdef CONFIG_ARM64
DECLARE_PER_CPU(unsigned long [IRQ_STACK_SIZE / sizeof(long)], irq_stack);
DECLARE_PER_CPU(unsigned long [THREAD_SIZE / sizeof(long)], vmap_stack);
#else
extern void *irq_stack[NR_CPUS];
#endif

int handle_vmap_fault(unsigned long addr,
		      unsigned int esr, struct pt_regs *regs);

void  __setup_vmap_stack(unsigned long off);
void  update_vmap_stack(int diff);
int   get_vmap_stack_size(void);
int   is_vmap_addr(unsigned long addr);
void  aml_stack_free(struct task_struct *tsk);
void *aml_stack_alloc(int node, struct task_struct *tsk);
void  aml_account_task_stack(struct task_struct *tsk, int account);
void vmap_report_meminfo(struct seq_file *m);
#ifdef CONFIG_ARM
int   on_vmap_irq_stack(unsigned long sp, int cpu);
#endif
struct page *check_pte_exist(unsigned long addr);
#endif /* __VMAP_STACK_H__ */
