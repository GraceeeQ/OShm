/* SPDX-License-Identifier: GPL-2.0-or-later */
/* internal.h: ramfs internal definitions
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

/* 自定义目录上下文结构体，用于传递同步信息 */
struct ramfs_dir_context {
    struct dir_context ctx;     // 标准目录上下文
    struct super_block *sb;     // ramfs 超级块
    const char *ramfs_path;     // ramfs 挂载点路径
    const char *sync_dir;       // 同步目录路径
};

struct file_path_pair {
    char *src_path;
    char *dst_path;
    struct list_head list;
};

extern const struct inode_operations ramfs_file_inode_operations;
int ramfs_file_flush(struct file *file);

static LIST_HEAD(file_path_list);
static DEFINE_SPINLOCK(file_path_lock);
