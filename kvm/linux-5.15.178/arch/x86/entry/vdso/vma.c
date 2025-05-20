// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2007 Andi Kleen, SUSE Labs.
 *
 * This contains most of the x86 vDSO kernel-side code.
 */
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/cpu.h>
#include <linux/ptrace.h>
#include <linux/time_namespace.h>

#include <asm/pvclock.h>
#include <asm/vgtod.h>
#include <asm/proto.h>
#include <asm/vdso.h>
#include <asm/mmu.h>
#include <asm/vvar.h>
#include <asm/tlb.h>
#include <asm/page.h>
#include <asm/desc.h>
#include <asm/cpufeature.h>
#include <clocksource/hyperv_timer.h>

#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/vm_event_item.h>

#include <linux/sched.h>
#include <linux/vdso_task.h>


// #define VTASK_SIZE  ALIGN(sizeof(struct task_struct), PAGE_SIZE)

#undef _ASM_X86_VVAR_H
#define EMIT_VVAR(name, offset)	\
	const size_t name ## _offset = offset;
#include <asm/vvar.h>

struct vdso_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_data *)(vvar_page + _vdso_data_offset);
}
#undef EMIT_VVAR

unsigned int vclocks_used __read_mostly;

#if defined(CONFIG_X86_64)
unsigned int __read_mostly vdso64_enabled = 1;
#endif

void __init init_vdso_image(const struct vdso_image *image)
{
	BUG_ON(image->size % PAGE_SIZE != 0);

	apply_alternatives((struct alt_instr *)(image->data + image->alt),
			   (struct alt_instr *)(image->data + image->alt +
						image->alt_len));
}

static const struct vm_special_mapping vvar_mapping;
struct linux_binprm;

static vm_fault_t vdso_fault(const struct vm_special_mapping *sm,
		      struct vm_area_struct *vma, struct vm_fault *vmf)
{
	const struct vdso_image *image = vma->vm_mm->context.vdso_image;

	if (!image || (vmf->pgoff << PAGE_SHIFT) >= image->size)
		return VM_FAULT_SIGBUS;

	vmf->page = virt_to_page(image->data + (vmf->pgoff << PAGE_SHIFT));
	get_page(vmf->page);
	return 0;
}

static void vdso_fix_landing(const struct vdso_image *image,
		struct vm_area_struct *new_vma)
{
#if defined CONFIG_X86_32 || defined CONFIG_IA32_EMULATION
	if (in_ia32_syscall() && image == &vdso_image_32) {
		struct pt_regs *regs = current_pt_regs();
		unsigned long vdso_land = image->sym_int80_landing_pad;
		unsigned long old_land_addr = vdso_land +
			(unsigned long)current->mm->context.vdso;

		/* Fixing userspace landing - look at do_fast_syscall_32 */
		if (regs->ip == old_land_addr)
			regs->ip = new_vma->vm_start + vdso_land;
	}
#endif
}

static int vdso_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	const struct vdso_image *image = current->mm->context.vdso_image;

	vdso_fix_landing(image, new_vma);
	current->mm->context.vdso = (void __user *)new_vma->vm_start;

	return 0;
}

#ifdef CONFIG_TIME_NS
static struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	if (likely(vma->vm_mm == current->mm))
		return current->nsproxy->time_ns->vvar_page;

	/*
	 * VM_PFNMAP | VM_IO protect .fault() handler from being called
	 * through interfaces like /proc/$pid/mem or
	 * process_vm_{readv,writev}() as long as there's no .access()
	 * in special_mapping_vmops().
	 * For more details check_vma_flags() and __access_remote_vm()
	 */

	WARN(1, "vvar_page accessed remotely");

	return NULL;
}

/*
 * The vvar page layout depends on whether a task belongs to the root or
 * non-root time namespace. Whenever a task changes its namespace, the VVAR
 * page tables are cleared and then they will re-faulted with a
 * corresponding layout.
 * See also the comment near timens_setup_vdso_data() for details.
 */
int vdso_join_timens(struct task_struct *task, struct time_namespace *ns)
{
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;

	mmap_read_lock(mm);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		unsigned long size = vma->vm_end - vma->vm_start;

		if (vma_is_special_mapping(vma, &vvar_mapping))
			zap_page_range(vma, vma->vm_start, size);
	}

	mmap_read_unlock(mm);
	return 0;
}
#else
static inline struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	return NULL;
}
#endif

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
		      struct vm_area_struct *vma, struct vm_fault *vmf)
{
	const struct vdso_image *image = vma->vm_mm->context.vdso_image;
	unsigned long pfn;
	long sym_offset;

	if (!image)
		return VM_FAULT_SIGBUS;

	sym_offset = (long)(vmf->pgoff << PAGE_SHIFT) +
		image->sym_vvar_start;

	/*
	 * Sanity check: a symbol offset of zero means that the page
	 * does not exist for this vdso image, not that the page is at
	 * offset zero relative to the text mapping.  This should be
	 * impossible here, because sym_offset should only be zero for
	 * the page past the end of the vvar mapping.
	 */
	if (sym_offset == 0)
		return VM_FAULT_SIGBUS;

	if (sym_offset == image->sym_vvar_page) {
		struct page *timens_page = find_timens_vvar_page(vma);

		pfn = __pa_symbol(&__vvar_page) >> PAGE_SHIFT;

		/*
		 * If a task belongs to a time namespace then a namespace
		 * specific VVAR is mapped with the sym_vvar_page offset and
		 * the real VVAR page is mapped with the sym_timens_page
		 * offset.
		 * See also the comment near timens_setup_vdso_data().
		 */
		if (timens_page) {
			unsigned long addr;
			vm_fault_t err;

			/*
			 * Optimization: inside time namespace pre-fault
			 * VVAR page too. As on timens page there are only
			 * offsets for clocks on VVAR, it'll be faulted
			 * shortly by VDSO code.
			 */
			addr = vmf->address + (image->sym_timens_page - sym_offset);
			err = vmf_insert_pfn(vma, addr, pfn);
			if (unlikely(err & VM_FAULT_ERROR))
				return err;

			pfn = page_to_pfn(timens_page);
		}

		return vmf_insert_pfn(vma, vmf->address, pfn);
	} else if (sym_offset == image->sym_pvclock_page) {
		struct pvclock_vsyscall_time_info *pvti =
			pvclock_get_pvti_cpu0_va();
		if (pvti && vclock_was_used(VDSO_CLOCKMODE_PVCLOCK)) {
			return vmf_insert_pfn_prot(vma, vmf->address,
					__pa(pvti) >> PAGE_SHIFT,
					pgprot_decrypted(vma->vm_page_prot));
		}
	} else if (sym_offset == image->sym_hvclock_page) {
		struct ms_hyperv_tsc_page *tsc_pg = hv_get_tsc_page();

		if (tsc_pg && vclock_was_used(VDSO_CLOCKMODE_HVCLOCK))
			return vmf_insert_pfn(vma, vmf->address,
					virt_to_phys(tsc_pg) >> PAGE_SHIFT);
	} else if (sym_offset == image->sym_timens_page) {
		struct page *timens_page = find_timens_vvar_page(vma);

		if (!timens_page)
			return VM_FAULT_SIGBUS;

		pfn = __pa_symbol(&__vvar_page) >> PAGE_SHIFT;
		return vmf_insert_pfn(vma, vmf->address, pfn);
	}

	return VM_FAULT_SIGBUS;
}

static vm_fault_t vtask_fault(
    const struct vm_special_mapping *sm,
    struct vm_area_struct *vma,
    struct vm_fault *vmf)
{
	struct task_struct *task = current;
    unsigned long task_phys_addr = virt_to_phys(task);
    unsigned long pfn = task_phys_addr >> PAGE_SHIFT;
    unsigned long task_offset = task_phys_addr & ~PAGE_MASK;  // 计算页内偏移量
    struct vdso_data *vdso_data;
	unsigned long metadata_pgoff = (VTASK_SIZE >> PAGE_SHIFT) - 1; // 最后一页的偏移量
   
    //printk(KERN_INFO "pass checkpoint 00\n");
	// printk(KERN_INFO "vmf->address = %lx\n", vmf->address);
	// printk(KERN_INFO "vmf->pgoff = %lx\n", vmf->pgoff);
	// printk(KERN_INFO "pfn = %lx\n", pfn);
	// printk(KERN_INFO "task_phys_addr = %lx\n", task_phys_addr);
	// printk(KERN_INFO "task = %lx\n", (unsigned long)task);printk(KERN_INFO "task offset = %lx\n", task_offset);
	// printk(KERN_INFO "vdso_data addr = %lx\n", (unsigned long)arch_get_vdso_data(current->mm->context.vdso));
	// printk(KERN_INFO "vma addr = %lx\n", (unsigned long)vma);
	// printk(KERN_INFO "metadata_pgoff = %lx\n", metadata_pgoff);
	
	/* 只在第一次页面错误时更新vdso_data中的偏移量 */
    // if (vmf->pgoff == 0) {
    //     /* 获取vdso_data的地址 */
    //     vdso_data = arch_get_vdso_data(current->mm->context.vdso);
    //     if (vdso_data) {
    //         /* 保存偏移量和大小到vdso_data */
    //         vdso_data->vtask_offset = task_offset;
    //         vdso_data->vtask_size = sizeof(struct task_struct);
    //         printk(KERN_INFO "Saved task_offset %lx to vdso_data\n", task_offset);
    //     }
    // }
	 if (vmf->pgoff == metadata_pgoff) {
        struct page *metadata_page;
        struct task_metadata {
            unsigned long magic;       /* 魔术数字，用于验证 */
            unsigned long task_offset; /* task_struct在页内的偏移量 */
            unsigned long task_size;   /* task_struct的总大小 */
            char reserved[4072];       /* 保留空间，凑满一页 */
        } *metadata;
        
        // printk(KERN_INFO "Creating metadata page at the last page\n");
        
        /* 分配新的物理页面用于元数据 */
        metadata_page = alloc_page(GFP_KERNEL);
        if (!metadata_page) {
            printk(KERN_ERR "Failed to allocate metadata page\n");
            return VM_FAULT_OOM;
        }
        
        /* 初始化元数据 */
        metadata = page_address(metadata_page);
        metadata->magic = 0x54534B4D; /* "TSKM" */
        metadata->task_offset = task_offset;
        metadata->task_size = sizeof(struct task_struct);
        memset(metadata->reserved, 0, sizeof(metadata->reserved));
        
        printk(KERN_INFO "Metadata page created with offset=%lx, size=%zu\n", 
               metadata->task_offset, metadata->task_size);
        
        /* 返回新分配的页面 */
        vmf->page = metadata_page;
        return 0;
    }
    /*
     * 只允许访问task_struct所在的页面范围内
     * 防止越界访问
     */
	//printk(KERN_INFO "pass checkpoint 01\n");
    if (vmf->pgoff >= (VTASK_SIZE >> PAGE_SHIFT))
        return VM_FAULT_SIGBUS;
    //printk(KERN_INFO "pass checkpoint 02\n");
    /* 将task_struct的物理页映射到用户空间 */
    return vmf_insert_pfn_prot(vma, vmf->address,
                             pfn + vmf->pgoff,
                             pgprot_noncached(vma->vm_page_prot));

}
// static vm_fault_t vtask_fault(
//     const struct vm_special_mapping *sm,
//     struct vm_area_struct *vma,
//     struct vm_fault *vmf)
// {
//     printk(KERN_INFO "vtask_fault: handling page fault at offset %lx\n", vmf->pgoff);
    
//     struct page *page;
//     struct task_struct *task = current;
//     void *src_addr, *dst_addr;
//     size_t copy_size;
    
//     /* 检查偏移量是否超出范围 */
//     if (vmf->pgoff >= (VTASK_SIZE >> PAGE_SHIFT)) {
//         printk(KERN_WARNING "vtask_fault: offset out of bounds %lx\n", vmf->pgoff);
//         return VM_FAULT_SIGBUS;
//     }
    
//     /* 分配新的页面 */
//     page = alloc_page(GFP_KERNEL);
//     if (!page) {
//         printk(KERN_WARNING "vtask_fault: failed to allocate page\n");
//         return VM_FAULT_OOM;
//     }
    
//     /* 计算源地址和目标地址 */
//     src_addr = (void *)task + (vmf->pgoff << PAGE_SHIFT);
//     dst_addr = page_address(page);
    
//     /* 确定要复制的大小 (不超过一个页面) */
//     copy_size = min_t(size_t, PAGE_SIZE, 
//                      sizeof(struct task_struct) - (vmf->pgoff << PAGE_SHIFT));
    
//     if (copy_size <= 0) {
//         /* 超出了task_struct范围，但在允许的页面偏移内 */
//         /* 为安全起见，填充零值 */
//         memset(dst_addr, 0, PAGE_SIZE);
//     } else {
//         /* 拷贝task_struct的内容到新页面 */
//         memcpy(dst_addr, src_addr, copy_size);
        
//         /* 如果不足一页，剩余部分填零 */
//         if (copy_size < PAGE_SIZE)
//             memset(dst_addr + copy_size, 0, PAGE_SIZE - copy_size);
//     }
    
//     /* 将页面映射到用户空间 */
//     vmf->page = page;
//     return 0;
// }


static const struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.fault = vdso_fault,
	.mremap = vdso_mremap,
};
static const struct vm_special_mapping vvar_mapping = {
	.name = "[vvar]",
	.fault = vvar_fault,
};
static const struct vm_special_mapping vtask_mapping = {
	.name = "[vtask]",
    .fault = vtask_fault,
};
static void print_stack_region(void)
{
    struct mm_struct *mm = current->mm;
    struct vm_area_struct *vma;
    bool found = false;

    if (!mm)
        return;

    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        if (vma->vm_flags & VM_STACK) {
            printk(KERN_INFO "[stack] region: 0x%lx - 0x%lx (size: %lu KB)\n", 
                vma->vm_start, vma->vm_end, (vma->vm_end - vma->vm_start) >> 10);
            printk(KERN_INFO "[stack] stack pointer: 0x%lx\n", current->thread.sp);
            printk(KERN_INFO "[stack] mm->start_stack: 0x%lx\n", mm->start_stack);
            found = true;
            break;
        }
    }

    if (!found) {
        printk(KERN_INFO "[stack] region not found\n");
    }
}
/*
 * Add vdso and vvar mappings to current process.
 * @image          - blob to map
 * @addr           - request a specific address (zero to map at free addr)
 */
static int map_vdso(const struct vdso_image *image, unsigned long addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long text_start;
	unsigned long vtask_addr = 0;    // 新增
	int ret = 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	addr = get_unmapped_area(NULL, addr,
				 image->size - image->sym_vvar_start, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	text_start = addr - image->sym_vvar_start;

	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 */
	// printk(KERN_INFO "addr = %lx", addr);
	printk(KERN_INFO "text_start = %lx, image->size = %lx\n", text_start, image->size);
	vma = _install_special_mapping(mm,
				       text_start,
				       image->size,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       &vdso_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}
	printk(KERN_INFO "addr = %lx, -image->sym_vvar_start = %lx\n", addr, -image->sym_vvar_start);
	vma = _install_special_mapping(mm,
				       addr,
				       -image->sym_vvar_start,
				       VM_READ|VM_MAYREAD|VM_IO|VM_DONTDUMP|
				       VM_PFNMAP,
				       &vvar_mapping);
	//printk(KERN_INFO "pass checkpoint 0\n");
	if (IS_ERR(vma)) {
        ret = PTR_ERR(vma);
        do_munmap(mm, text_start, image->size, NULL);
        goto up_fail;
    }
	//printk(KERN_INFO "pass checkpoint 1\n");
	vtask_addr = addr - VTASK_SIZE;
	printk(KERN_INFO "vtask_addr = %lx, VTASK_SIZE = %lx\n", vtask_addr, VTASK_SIZE);
    vma = _install_special_mapping(mm,
                       vtask_addr,
                       VTASK_SIZE,
                       VM_READ|VM_MAYREAD|VM_IO|VM_DONTDUMP|VM_PFNMAP|VM_WIPEONFORK,
                       &vtask_mapping);
	//printk(KERN_INFO "pass checkpoint 2\n");
    if (IS_ERR(vma)) {
		printk(KERN_INFO "into errrr\n");
		print_stack_region();
        ret = PTR_ERR(vma);
        do_munmap(mm, addr, -image->sym_vvar_start, NULL);
        do_munmap(mm, text_start, image->size, NULL);
		goto up_fail;
    } 
	//printk(KERN_INFO "pass checkpoint 3\n");
    current->mm->context.vdso = (void __user *)text_start;
    current->mm->context.vdso_image = image;
	//printk(KERN_INFO "pass checkpoint 4\n");
    current->mm->context.vtask = (void __user *)vtask_addr;  // 保存vtask映射地址

up_fail:
	mmap_write_unlock(mm);
	return ret;
}

#ifdef CONFIG_X86_64
/*
 * Put the vdso above the (randomized) stack with another randomized
 * offset.  This way there is no hole in the middle of address space.
 * To save memory make sure it is still in the same PTE as the stack
 * top.  This doesn't give that many random bits.
 *
 * Note that this algorithm is imperfect: the distribution of the vdso
 * start address within a PMD is biased toward the end.
 *
 * Only used for the 64-bit and x32 vdsos.
 */
static unsigned long vdso_addr(unsigned long start, unsigned len)
{
	unsigned long addr, end;
	unsigned offset;

	/*
	 * Round up the start address.  It can start out unaligned as a result
	 * of stack start randomization.
	 */
	start = PAGE_ALIGN(start);

	/* Round the lowest possible end address up to a PMD boundary. */
	end = (start + len + PMD_SIZE - 1) & PMD_MASK;
	if (end >= DEFAULT_MAP_WINDOW)
		end = DEFAULT_MAP_WINDOW;
	end -= len;

	if (end > start) {
		offset = get_random_int() % (((end - start) >> PAGE_SHIFT) + 1);
		addr = start + (offset << PAGE_SHIFT);
	} else {
		addr = start;
	}

	/*
	 * Forcibly align the final address in case we have a hardware
	 * issue that requires alignment for performance reasons.
	 */
	addr = align_vdso_addr(addr);

	return addr;
}

static int map_vdso_randomized(const struct vdso_image *image)
{
	unsigned long addr = vdso_addr(current->mm->start_stack, image->size-image->sym_vvar_start);

	// printk(KERN_INFO "map_vdso_randomized: addr = %lx\n", addr);
	return map_vdso(image, addr);
}
#endif

int map_vdso_once(const struct vdso_image *image, unsigned long addr)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	mmap_write_lock(mm);
	/*
	 * Check if we have already mapped vdso blob - fail to prevent
	 * abusing from userspace install_special_mapping, which may
	 * not do accounting and rlimit right.
	 * We could search vma near context.vdso, but it's a slowpath,
	 * so let's explicitly check all VMAs to be completely sure.
	 */
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma_is_special_mapping(vma, &vdso_mapping) ||
				vma_is_special_mapping(vma, &vvar_mapping)
				||vma_is_special_mapping(vma, &vtask_mapping)
			) {
			mmap_write_unlock(mm);
			return -EEXIST;
		}
	}
	mmap_write_unlock(mm);
	// printk(KERN_INFO "map_vdso_once: addr = %lx\n", addr);
	return map_vdso(image, addr);
}

#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
static int load_vdso32(void)
{
	if (vdso32_enabled != 1)  /* Other values all mean "disabled" */
		return 0;

	return map_vdso(&vdso_image_32, 0);
}
#endif

#ifdef CONFIG_X86_64
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	if (!vdso64_enabled)
		return 0;
	// printk(KERN_INFO "arch_setup_additional_pages: addr = %lx\n", 0);
	return map_vdso_randomized(&vdso_image_64);
}

#ifdef CONFIG_COMPAT
int compat_arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp, bool x32)
{
#ifdef CONFIG_X86_X32_ABI
	if (x32) {
		if (!vdso64_enabled)
			return 0;
		return map_vdso_randomized(&vdso_image_x32);
	}
#endif
#ifdef CONFIG_IA32_EMULATION
	return load_vdso32();
#else
	return 0;
#endif
}
#endif
#else
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	return load_vdso32();
}
#endif

bool arch_syscall_is_vdso_sigreturn(struct pt_regs *regs)
{
#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
	const struct vdso_image *image = current->mm->context.vdso_image;
	unsigned long vdso = (unsigned long) current->mm->context.vdso;

	if (in_ia32_syscall() && image == &vdso_image_32) {
		if (regs->ip == vdso + image->sym_vdso32_sigreturn_landing_pad ||
		    regs->ip == vdso + image->sym_vdso32_rt_sigreturn_landing_pad)
			return true;
	}
#endif
	return false;
}

#ifdef CONFIG_X86_64
static __init int vdso_setup(char *s)
{
	vdso64_enabled = simple_strtoul(s, NULL, 0);
	return 1;
}
__setup("vdso=", vdso_setup);

static int __init init_vdso(void)
{
	BUILD_BUG_ON(VDSO_CLOCKMODE_MAX >= 32);

	init_vdso_image(&vdso_image_64);

#ifdef CONFIG_X86_X32_ABI
	init_vdso_image(&vdso_image_x32);
#endif

	return 0;
}
subsys_initcall(init_vdso);
#endif /* CONFIG_X86_64 */
