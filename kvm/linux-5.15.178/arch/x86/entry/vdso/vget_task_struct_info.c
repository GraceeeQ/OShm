// SPDX-License-Identifier: GPL-2.0
/*
 * vDSO implementation for exposing task_struct to user space
 */

#include <linux/time.h>
#include <linux/types.h>
#include <linux/vdso_task.h>
#include <asm/unistd.h>
#include <asm/vgtod.h>
#include <asm/vdso.h>
#include <asm/vvar.h>
#include <asm/processor.h>

/*
 * 这个函数导出给用户空间，用于获取当前任务的task_struct信息
 */
int __vdso_get_task_struct_info(struct task_info *info)
{
    const struct vdso_data *vdata = __arch_get_vdso_data();
    void __user *vtask_base;
    unsigned long metadata_offset;
    struct task_metadata {
        unsigned long magic;
        unsigned long task_offset;
        unsigned long task_size;
        char reserved[4072];
    } *metadata;
    if (!info)
        return -EINVAL;
    
    /* 
     * 使用vdso_data作为基础，计算vtask位置
     * vdso_data结构总是位于vvar页的开头，所以可以用它作为参考点
     */
    vtask_base = (void __user *)(((unsigned long)vdata >> PAGE_SHIFT << PAGE_SHIFT) -  
                           VVAR_TASK_STRUCT_NR_PAGES * PAGE_SIZE);
    if (!vtask_base)
        return -EINVAL;
    metadata_offset = VTASK_SIZE - PAGE_SIZE;
    metadata = (struct task_metadata *)((char *)vtask_base + metadata_offset);
    if (metadata->magic != 0x54534B4D)
        return -EINVAL;
    unsigned long task_offset = metadata->task_offset;
    // unsigned long task_offset = 0;
    
    /* 从映射的task_struct中直接读取PID */
    info->task_struct_ptr = (void *)((char *)vtask_base + task_offset);
    // info->pid = *(pid_t *)((char *)vtask_base + task_offset + offsetof(struct task_struct, pid));
    info->pid = *(pid_t *)((char *)info->task_struct_ptr + 
                          offsetof(struct task_struct, pid));
    
    return 0;
}


int get_task_struct_info(struct task_info *info)
    __attribute__((weak, alias("__vdso_get_task_struct_info")));

// /*
//  * 强类型版本，用于通过C标准库调用
//  */
// int get_task_struct_info(struct task_info *info)
// {
//     return __vdso_get_task_struct_info(info);
// }

// /*
//  * 为了安全起见，我们也提供一个内部系统调用的入口点
//  * 当vDSO实现不可用时，会回退到这个系统调用
//  */
// int __vdso_get_task_struct_info_fallback(struct task_info *info)
// {
//     long ret;
    
//     /* 定义一个内核未实现的系统调用号，会导致回退到慢速路径 */
//     ret = -ENOSYS;
    
//     /*
//      * 如果支持的话，实现一个真正的系统调用
//      * 现在先回退到错误
//      */
    
//     return ret;
// }

/*
 * 注意: 这个文件需要添加到arch/x86/entry/vdso/Makefile中的编译列表
 */