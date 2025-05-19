#ifndef _UAPI_LINUX_VDSO_TASK_H
#define _UAPI_LINUX_VDSO_TASK_H

#include <linux/types.h>

/**
 * struct task_info - 暴露给用户空间的任务信息
 * @pid: 进程ID
 * @task_struct_ptr: 指向映射到用户空间的task_struct的指针
 */
struct task_info {
    pid_t pid;
    void *task_struct_ptr;
};

/**
 * get_task_struct_info - 获取当前任务的信息
 * @info: 用于存储任务信息的指针
 *
 * 返回值: 成功返回0，失败返回错误码
 */
int get_task_struct_info(struct task_info *info);

#endif /* _UAPI_LINUX_VDSO_TASK_H */