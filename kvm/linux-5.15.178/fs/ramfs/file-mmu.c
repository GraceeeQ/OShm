/* file-mmu.c: ramfs MMU-based file operations
 *
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ramfs.h>
#include <linux/sched.h>

#include "internal.h"

static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
						 unsigned long addr,
						 unsigned long len,
						 unsigned long pgoff,
						 unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}
// 添加 fsync 操作实现

static int ramfs_fsync(struct file *file, loff_t start, loff_t end,
               int datasync)
{
    int ret;
    
    // 检查文件是否有读权限
    if (!(file->f_mode & FMODE_READ)) {
        // 文件没有读权限，需要重新以读方式打开
        struct file *read_file;
        char *path_buf = NULL;
        char *path_str = NULL;
        
        // 获取当前文件路径
        path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
        if (!path_buf)
            return -ENOMEM;
            
        path_str = d_path(&file->f_path, path_buf, PATH_MAX);
        if (IS_ERR(path_str)) {
            kfree(path_buf);
            return PTR_ERR(path_str);
        }
        
        // 以读方式重新打开文件
        read_file = filp_open(path_str, O_RDONLY, 0);
        kfree(path_buf);
        
        if (IS_ERR(read_file))
            return PTR_ERR(read_file);
            
        // 使用有读权限的文件执行同步
        ret = ramfs_file_flush(read_file);
        filp_close(read_file, NULL);
        
        return ret;
    } else {
        // 文件有读权限，直接调用持久化接口
        return ramfs_file_flush(file);
    }
}

static ssize_t ramfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct ramfs_fs_info *fsi = sb->s_fs_info;
	unsigned int orig_f_mode;
    int sync_ret;

	// 首先执行原始的写操作
	ret = generic_file_write_iter(iocb, from);

	// 如果写入成功且已配置同步目录，则执行同步
	if (ret > 0 && fsi && fsi->sync_dir && !(file->f_mode & FMODE_READ)) {
        // 重新打开文件，添加读权限
        struct file *read_file;
        char *path_buf = NULL;
        char *path_str = NULL;
        
        // 获取当前文件路径
        path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
        if (!path_buf)
            return ret; // 返回原始写入结果，不同步
            
        path_str = d_path(&file->f_path, path_buf, PATH_MAX);
        if (IS_ERR(path_str)) {
            kfree(path_buf);
            return ret; // 返回原始写入结果，不同步
        }
        
        // 以读写方式重新打开文件
        read_file = filp_open(path_str, O_RDONLY, 0);
        kfree(path_buf);
        
        if (IS_ERR(read_file))
            return ret; // 返回原始写入结果，不同步
            
        // 使用有读权限的文件执行同步
        sync_ret = ramfs_file_flush(read_file);
        filp_close(read_file, NULL);
        
        if (sync_ret)
            pr_warn("ramfs: 文件自动同步失败，错误码 %d\n", sync_ret);
    } else if (ret > 0 && fsi && fsi->sync_dir) {
        // 文件已有读权限，直接同步
        sync_ret = ramfs_file_flush(file);
        if (sync_ret)
            pr_warn("ramfs: 文件自动同步失败，错误码 %d\n", sync_ret);
    }

	return ret;
}

const struct file_operations ramfs_file_operations = {
	.read_iter = generic_file_read_iter,
	.write_iter = ramfs_file_write_iter,
	.mmap = generic_file_mmap,
	.fsync = ramfs_fsync,
	.splice_read = generic_file_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = generic_file_llseek,
	.get_unmapped_area = ramfs_mmu_get_unmapped_area,
};

const struct inode_operations ramfs_file_inode_operations = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
};
