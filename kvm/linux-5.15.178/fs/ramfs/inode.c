/*
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
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include "internal.h"

#include <linux/file.h>
#include <linux/fs_struct.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
/* 文件持久化接口 */
int ramfs_bind(const char *ramfs_path, const char *sync_dir);
int ramfs_file_flush(struct file *file);
static int ramfs_readdir_sync(struct dir_context *ctx, const char *name, 
                             int namlen, loff_t offset, u64 ino, 
                             unsigned int d_type);

static int copy_file_content(struct file *src, struct file *dst);


#define RAMFS_DEFAULT_MODE	0755

static const struct super_operations ramfs_ops;
static const struct inode_operations ramfs_dir_inode_operations;
/* 声明全局变量 */
static struct proc_dir_entry *ramfs_proc_root;
static struct proc_dir_entry *ramfs_proc_bind;
static struct proc_dir_entry *ramfs_proc_sync;

/* 定义 proc 绑定接口的写入操作 */
static ssize_t ramfs_proc_bind_write(struct file *file, const char __user *buffer,
                                    size_t count, loff_t *pos)
{
    char *kbuf, *ramfs_path, *sync_dir;
    int ret;
    
    kbuf = kmalloc(count + 1, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;
        
    if (copy_from_user(kbuf, buffer, count)) {
        kfree(kbuf);
        return -EFAULT;
    }
    
    kbuf[count] = '\0';
    
    /* 解析参数: 格式为 "ramfs_path sync_dir" */
    ramfs_path = kbuf;
    sync_dir = strchr(kbuf, ' ');
    if (!sync_dir) {
        pr_err("ramfs_bind: 无效参数格式，需要 \"ramfs_path sync_dir\"\n");
        kfree(kbuf);
        return -EINVAL;
    }
    
    *sync_dir = '\0';
    sync_dir++;
    
    /* 去除空白 */
    while (*sync_dir == ' ')
        sync_dir++;

    /* 去除尾部换行符和回车符 */
    char *end = sync_dir + strlen(sync_dir) - 1;
    while (end >= sync_dir && (*end == '\n' || *end == '\r'))
        *end-- = '\0';

    /* 调用绑定函数 */
    ret = ramfs_bind(ramfs_path, sync_dir);
    
    kfree(kbuf);
    return (ret == 0) ? count : ret;
}

/* 定义 proc 同步接口的写入操作 */
static ssize_t ramfs_proc_sync_write(struct file *file, const char __user *buffer,
                                    size_t count, loff_t *pos)
{
    char *kbuf;
    int ret = 0;
    struct file *target_file = NULL;
	int i=0;
	int only_newlines=0;
	int has_content;
    int original_count = count;
    // printk(KERN_INFO "buffer: %s\n", buffer);
    /* 分配内核缓冲区 */
    kbuf = kmalloc(count + 1, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;
        
    /* 从用户空间复制数据 */
    if (copy_from_user(kbuf, buffer, count)) {
        kfree(kbuf);
        return -EFAULT;
    }
    
    kbuf[count] = '\0';

    /* 检查是否有实际内容 */
    for (i = 0; i < count; i++) {
        if (kbuf[i] != '\n' && kbuf[i] != '\r' && kbuf[i] != ' ' && kbuf[i] != '\t') {
            has_content = 1;
            break;
        }
    }
    
    /* 如果没有实际内容，直接返回成功 */
    if (!has_content) {
        printk(KERN_INFO "ramfs_sync: 空内容，忽略请求\n");
        kfree(kbuf);
        return original_count;  // 返回成功但不执行任何操作
    }
    
    /* 去除尾部空白 */
    while (count > 0 && (kbuf[count-1] == '\n' || kbuf[count-1] == '\r'))
    kbuf[--count] = '\0';
    // 如果处理后路径为空，不要尝试打开文件
    if (count == 0) {
        kfree(kbuf);
        return -EINVAL;
    }
    printk(KERN_INFO "kbuf: -%s-\n", kbuf);
    /* 打开文件 */
    target_file = filp_open(kbuf, O_RDWR, 0);
    if (IS_ERR(target_file)) {
        pr_err("ramfs_sync: 无法打开文件 %s, 错误 %ld\n", 
               kbuf, PTR_ERR(target_file));
        kfree(kbuf);
        return PTR_ERR(target_file);
    }
    //printk(KERN_INFO "checkpoint1\n");
    /* 调用同步函数 */
    ret = ramfs_file_flush(target_file);
    //printk(KERN_INFO "checkpoint2\n");
    /* 关闭文件 */
    filp_close(target_file, NULL);
    kfree(kbuf);
    
    return (ret == 0) ? original_count : ret;
}

/* 定义 proc 文件操作 */
static const struct proc_ops ramfs_proc_bind_ops = {
    .proc_write = ramfs_proc_bind_write,
};

static const struct proc_ops ramfs_proc_sync_ops = {
    .proc_write = ramfs_proc_sync_write,
};

/* 初始化 proc 接口 */
static int __init ramfs_proc_init(void)
{
    /* 创建/proc/fs/ramfs目录 */
    ramfs_proc_root = proc_mkdir("fs/ramfs", NULL);
    if (!ramfs_proc_root)
        return -ENOMEM;
    
    /* 创建/proc/fs/ramfs/bind文件 */
    ramfs_proc_bind = proc_create("bind", 0200, ramfs_proc_root, &ramfs_proc_bind_ops);
    if (!ramfs_proc_bind) {
        proc_remove(ramfs_proc_root);
        return -ENOMEM;
    }
    
    /* 创建/proc/fs/ramfs/sync文件 */
    ramfs_proc_sync = proc_create("sync", 0200, ramfs_proc_root, &ramfs_proc_sync_ops);
    if (!ramfs_proc_sync) {
        proc_remove(ramfs_proc_bind);
        proc_remove(ramfs_proc_root);
        return -ENOMEM;
    }
    
    pr_info("ramfs: 持久化测试接口已创建\n");
    return 0;
}

/* 清理 proc 接口 */
static void __exit ramfs_proc_exit(void)
{
    if (ramfs_proc_sync)
        proc_remove(ramfs_proc_sync);
    if (ramfs_proc_bind)
        proc_remove(ramfs_proc_bind);
    if (ramfs_proc_root)
        proc_remove(ramfs_proc_root);
}

struct ramfs_mount_opts {
	umode_t mode;
};

struct ramfs_fs_info {
	struct ramfs_mount_opts mount_opts;
    char *sync_dir;          /* 持久化同步目录路径 */
};



struct inode *ramfs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(&init_user_ns, inode, dir, mode);
		inode->i_mapping->a_ops = &ram_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &ramfs_file_inode_operations;
			inode->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &ramfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
ramfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	    struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = ramfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int ramfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	int retval = ramfs_mknod(&init_user_ns, dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return ramfs_mknod(&init_user_ns, dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = ramfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(inode);
	}
	return error;
}

static int ramfs_tmpfile(struct user_namespace *mnt_userns,
			 struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = ramfs_get_inode(dir->i_sb, dir, mode, 0);
	if (!inode)
		return -ENOSPC;
	d_tmpfile(dentry, inode);
	return 0;
}

static const struct inode_operations ramfs_dir_inode_operations = {
	.create		= ramfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= ramfs_symlink,
	.mkdir		= ramfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= ramfs_mknod,
	.rename		= simple_rename,
	.tmpfile	= ramfs_tmpfile,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int ramfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct ramfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != RAMFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);
	return 0;
}

static const struct super_operations ramfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= ramfs_show_options,
};

enum ramfs_param {
	Opt_mode,
};

const struct fs_parameter_spec ramfs_fs_parameters[] = {
	fsparam_u32oct("mode",	Opt_mode),
	{}
};

static int ramfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct ramfs_fs_info *fsi = fc->s_fs_info;
	int opt;

	opt = fs_parse(fc, ramfs_fs_parameters, param, &result);
	if (opt < 0) {
		/*
		 * We might like to report bad mount options here;
		 * but traditionally ramfs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		if (opt == -ENOPARAM)
			opt = 0;
		return opt;
	}

	switch (opt) {
	case Opt_mode:
		fsi->mount_opts.mode = result.uint_32 & S_IALLUGO;
		break;
	}

	return 0;
}

static int ramfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct ramfs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= RAMFS_MAGIC;
	sb->s_op		= &ramfs_ops;
	sb->s_time_gran		= 1;

	// 初始化同步目录为 NULL
    fsi->sync_dir = NULL;

	inode = ramfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static int ramfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, ramfs_fill_super);
}

static void ramfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations ramfs_context_ops = {
	.free		= ramfs_free_fc,
	.parse_param	= ramfs_parse_param,
	.get_tree	= ramfs_get_tree,
};

int ramfs_init_fs_context(struct fs_context *fc)
{
	struct ramfs_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	fsi->mount_opts.mode = RAMFS_DEFAULT_MODE;
	fc->s_fs_info = fsi;
	fc->ops = &ramfs_context_ops;
	return 0;
}

void ramfs_kill_sb(struct super_block *sb)
{
	struct ramfs_fs_info *fsi = sb->s_fs_info;
    
    if (fsi && fsi->sync_dir)
        kfree(fsi->sync_dir);

	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type ramfs_fs_type = {
	.name		= "ramfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters	= ramfs_fs_parameters,
	.kill_sb	= ramfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

static int __init init_ramfs_fs(void)
{
	int ret = register_filesystem(&ramfs_fs_type);
    if (ret == 0)
        ramfs_proc_init();  // 添加这行，初始化 proc 接口
    return ret;
}
fs_initcall(init_ramfs_fs);

/* 将 ramfs 与持久化目录绑定 */
int ramfs_bind(const char *ramfs_path, const char *sync_dir)
{
    struct path ramfs_mount, sync_path;
    struct super_block *sb;
    struct ramfs_fs_info *fsi;
    struct file *dir;
    int error;
    printk(KERN_INFO "ramfs_bind: 尝试绑定--%s--到--%s--\n", 
		   ramfs_path, sync_dir);

	error = kern_path(sync_dir, LOOKUP_FOLLOW, &sync_path);
    if (error) {
        pr_err("ramfs_bind: 无法访问同步目录 %s, 错误码 %d\n", 
               sync_dir, error);
        path_put(&ramfs_mount);
        return error;
    }

    /* 参数有效性检查 */
    if (!ramfs_path || !sync_dir)
        return -EINVAL;
    
    /* 检查ramfs路径是否存在 */
    error = kern_path(ramfs_path, LOOKUP_FOLLOW, &ramfs_mount);
    if (error) {
        pr_err("ramfs_bind: 无法访问ramfs路径 %s, 错误码 %d\n", 
               ramfs_path, error);
        return error;
    }
    
    /* 检查是否为ramfs文件系统 */
    sb = ramfs_mount.dentry->d_sb;
    if (!sb || sb->s_magic != RAMFS_MAGIC) {
        pr_err("ramfs_bind: %s 不是ramfs文件系统\n", ramfs_path);
        path_put(&ramfs_mount);
        return -EINVAL;
    }
    
    /* 检查同步目录是否存在 */
    error = kern_path(sync_dir, LOOKUP_FOLLOW, &sync_path);
    if (error) {
        pr_err("ramfs_bind: 无法访问同步目录 %s, 错误码 %d\n", 
               sync_dir, error);
        path_put(&ramfs_mount);
        return error;
    }

    /* 确保同步目录是目录 */
    if (!S_ISDIR(d_inode(sync_path.dentry)->i_mode)) {
        pr_err("ramfs_bind: %s 不是一个目录\n", sync_dir);
        path_put(&ramfs_mount);
        path_put(&sync_path);
        return -ENOTDIR;
    }
    
    /* 获取并更新 ramfs 的文件系统信息 */
    fsi = sb->s_fs_info;
    if (!fsi) {
        pr_err("ramfs_bind: 无法获取文件系统信息\n");
        path_put(&ramfs_mount);
        path_put(&sync_path);
        return -EINVAL;
    }
    
    /* 释放旧的同步目录路径（如果存在） */
    if (fsi->sync_dir) {
        kfree(fsi->sync_dir);
        fsi->sync_dir = NULL;
    }
    
    /* 保存新的同步目录路径 */
    fsi->sync_dir = kstrdup(sync_dir, GFP_KERNEL);
    if (!fsi->sync_dir) {
        pr_err("ramfs_bind: 内存分配失败\n");
        path_put(&ramfs_mount);
        path_put(&sync_path);
        return -ENOMEM;
    }
    
    /* 打开同步目录以遍历文件 */
    dir = dentry_open(&sync_path, O_RDONLY | O_DIRECTORY, current_cred());
    path_put(&sync_path);  /* 释放路径，后面通过 dir 操作 */
    
    if (IS_ERR(dir)) {
        pr_err("ramfs_bind: 无法打开同步目录，错误码 %ld\n", PTR_ERR(dir));
        /* 保留超级块中的同步目录路径，可能后续会修复同步目录问题 */
        path_put(&ramfs_mount);
        return PTR_ERR(dir);
    }
    
    /* 设置目录上下文，包含所需的所有信息 */
    struct ramfs_dir_context rdc = {
        .ctx = {
            .actor = ramfs_readdir_sync,
            .pos = 0,
        },
        .sb = sb,
        .ramfs_path = ramfs_path,  /* 现在传递ramfs路径 */
        .sync_dir = fsi->sync_dir
    };
    
    /* 遍历目录并恢复文件 */
    pr_info("ramfs_bind: 从 %s 同步文件到 %s\n", sync_dir, ramfs_path);
    error = iterate_dir(dir, &rdc.ctx);
    
    filp_close(dir, NULL);
    path_put(&ramfs_mount);
    
    if (error)
        pr_err("ramfs_bind: 同步过程中发生错误，错误码 %d\n", error);
    else
        pr_info("ramfs_bind: 同步完成\n");
    
    return error;
}

/* 目录迭代回调函数 - 恢复文件到 ramfs */
static int ramfs_readdir_sync(struct dir_context *ctx, const char *name, 
                             int namlen, loff_t offset, u64 ino, 
                             unsigned int d_type)
{
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint0\n");
    struct ramfs_dir_context *rdc = container_of(ctx, struct ramfs_dir_context, ctx);
    struct super_block *sb = rdc->sb;
    struct path ramfs_target;
    struct path source_path;
    struct file *src_file = NULL, *dst_file = NULL;
    char *src_path = NULL, *dst_path = NULL;
    int error = 0;
    
    /* 跳过 . 和 .. 目录 */
    if (name[0] == '.' && (namlen == 1 || (name[1] == '.' && namlen == 2)))
        return 0;
    
    /* 跳过临时文件 */
    if (name[0] == '.' && strstr(name, ".tmp"))
        return 0;
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint1\n");
    /* 分配路径缓冲区 */
    src_path = kmalloc(PATH_MAX, GFP_KERNEL);
    dst_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!src_path || !dst_path) {
        error = -ENOMEM;
        goto out_free;
    }
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint2\n");
    /* 构建源文件完整路径（同步目录中的文件） */
    snprintf(src_path, PATH_MAX, "%s/%s", rdc->sync_dir, name);
    
    /* 构建目标文件路径（ramfs中的文件） */
    snprintf(dst_path, PATH_MAX, "%s/%s", rdc->ramfs_path, name);
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint3\n");
    /* 获取源文件信息 */
    error = kern_path(src_path, LOOKUP_FOLLOW, &source_path);
    if (error) {
        pr_warn("ramfs_readdir_sync: 无法访问源文件 %s, 错误码 %d\n", 
                src_path, error);
        goto out_free;
    }
    
    /* 仅处理普通文件 */
    if (!S_ISREG(d_inode(source_path.dentry)->i_mode)) {
        pr_info("ramfs_readdir_sync: 跳过非文件项 %s\n", src_path);
        path_put(&source_path);
        goto out_free;  /* 不是错误，只是跳过 */
    }
    
    /* 打开源文件 */
    src_file = dentry_open(&source_path, O_RDONLY, current_cred());
    path_put(&source_path);
    if (IS_ERR(src_file)) {
        error = PTR_ERR(src_file);
        src_file = NULL;
        pr_err("ramfs_readdir_sync: 无法打开源文件 %s, 错误码 %d\n", 
               src_path, error);
        goto out_free;
    }
    
    /* 创建目标文件的父目录（如果不存在） */
    char *last_slash = strrchr(dst_path, '/');
    if (last_slash) {
        *last_slash = '\0';  /* 临时截断路径以获取目录部分 */
        error = kern_path(dst_path, LOOKUP_FOLLOW, &ramfs_target);
        if (error) {
            /* 目录不存在，尝试创建 */
            pr_info("ramfs_readdir_sync: 父目录 %s 不存在，尝试创建\n", dst_path);
            /* 这里应该实现目录创建逻辑，但简化版本中略过 */
        } else {
            path_put(&ramfs_target);
        }
        *last_slash = '/';  /* 恢复路径 */
    }
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint4\n");
    /* 打开或创建目标文件 */
    dst_file = filp_open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(dst_file)) {
        error = PTR_ERR(dst_file);
        dst_file = NULL;
        pr_err("ramfs_readdir_sync: 无法创建目标文件 %s, 错误码 %d\n", 
               dst_path, error);
        goto out_close;
    }
    
    /* 复制文件内容 */
    error = copy_file_content(src_file, dst_file);
    if (error) {
        pr_err("ramfs_readdir_sync: 复制文件 %s 到 %s 失败，错误码 %d\n", 
               src_path, dst_path, error);
    } else {
        pr_debug("ramfs_readdir_sync: 成功恢复 %s 到 %s\n", 
                 src_path, dst_path);
    }
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint5\n");
out_close:
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint6\n");
    if (dst_file && !IS_ERR(dst_file))
        filp_close(dst_file, NULL);
    if (src_file && !IS_ERR(src_file))
        filp_close(src_file, NULL);
    
out_free:
    printk(KERN_INFO "ramfs_readdir_sync: checkpoint7\n");
    kfree(src_path);
    kfree(dst_path);
    return error;
}

/* 高效复制文件内容 */
static int copy_file_content(struct file *src, struct file *dst)
{
    void *buf;
    loff_t src_pos = 0, dst_pos = 0;
    ssize_t bytes_read, bytes_written;
    int ret = 0;
    const size_t buf_size = PAGE_SIZE * 4;  /* 使用更大的缓冲区提高效率 */
    
    /* 分配复制缓冲区 */
    buf = kvmalloc(buf_size, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;
    
    /* 复制循环 */
    while (1) {
        /* 读取源文件块 */
        bytes_read = kernel_read(src, buf, buf_size, &src_pos);
        if (bytes_read <= 0) {
            if (bytes_read < 0)
                ret = bytes_read;  /* 读取出错 */
            break;
        }
        
        /* 写入目标文件 */
        bytes_written = kernel_write(dst, buf, bytes_read, &dst_pos);
        if (bytes_written < bytes_read) {
            ret = (bytes_written < 0) ? bytes_written : -EIO;
            pr_err("copy_file_content: 写入失败，写入 %zd/%zd 字节\n", 
                   bytes_written, bytes_read);
            break;
        }
    }
    
    /* 确保写入已完成 */
    if (ret == 0) {
        ret = vfs_fsync(dst, 0);
        if (ret)
            pr_err("copy_file_content: fsync 失败，错误码 %d\n", ret);
    }
    
    /* 释放资源 */
    kvfree(buf);
    return ret;
}

/* 将文件从 ramfs 同步到持久化目录 */
int ramfs_file_flush(struct file *file)
{
    struct inode *inode;
    struct super_block *sb;
    struct ramfs_fs_info *fsi;
    struct file *dest_file = NULL;
    char *path_buf = NULL, *dest_path = NULL, *tmp_path = NULL, *final_path = NULL;
    struct path src_path;
    int ret = 0;
    //printk(KERN_INFO "ramfs_file_flush: checkpoint0\n");
    /* 基本检查 */
    if (!file)
        return -EINVAL;
    
    inode = file_inode(file);
    if (!inode)
        return -EINVAL;
    
    sb = inode->i_sb;
    if (!sb)
        return -EINVAL;
    
    fsi = sb->s_fs_info;
    if (!fsi || !fsi->sync_dir)
        return -EINVAL;
    
    /* 获取文件路径和名称 */
    path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!path_buf)
        return -ENOMEM;
    //printk(KERN_INFO "ramfs_file_flush: checkpoint1\n");
    src_path = file->f_path;
    dest_path = d_path(&src_path, path_buf, PATH_MAX);
    if (IS_ERR(dest_path)) {
        ret = PTR_ERR(dest_path);
        goto out_free_path;
    }
    
    /* 提取文件名 */
    char *filename = strrchr(dest_path, '/');
    if (!filename) {
        filename = dest_path;  /* 没有斜杠，使用整个路径 */
    } else {
        filename++;  /* 跳过斜杠 */
    }
    
    /* 分配临时文件路径 */
    tmp_path = kmalloc(PATH_MAX, GFP_KERNEL);
    final_path = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!tmp_path || !final_path) {
        ret = -ENOMEM;
        goto out_free_all;
    }
    
    /* 构建临时文件路径 - 使用时间戳确保唯一性 */
    snprintf(tmp_path, PATH_MAX, "%s/.%s.%lx.tmp", 
             fsi->sync_dir, filename, (unsigned long)ktime_get_real_seconds());
    //printk(KERN_INFO "ramfs_file_flush: checkpoint2\n");
    /* 构建最终文件路径 */
    snprintf(final_path, PATH_MAX, "%s/%s", fsi->sync_dir, filename);
    
    /* 打开临时目标文件 */
    dest_file = filp_open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(dest_file)) {
        ret = PTR_ERR(dest_file);
        dest_file = NULL;
        pr_err("ramfs_file_flush: 无法创建临时文件 %s, 错误码 %d\n", 
               tmp_path, ret);
        goto out_free_all;
    }
    
    /* 复制文件内容到临时文件 */
    ret = copy_file_content(file, dest_file);
    if (ret) {
        pr_err("ramfs_file_flush: 复制内容到临时文件失败，错误码 %d\n", ret);
        goto out_close;
    }
    
    /* 关闭文件以确保所有内容已写入 */
    filp_close(dest_file, NULL);
    dest_file = NULL;
    
    /* 原子地重命名临时文件为最终文件 */
    struct path old_path, new_dir_path;
    struct dentry *new_dentry;
    int error;
    //printk(KERN_INFO "ramfs_file_flush: checkpoint3\n");
    /* 获取源文件路径 */
    error = kern_path(tmp_path, 0, &old_path);
    if (error) {
        pr_err("ramfs_file_flush: 无法查找源路径 %s, 错误码 %d\n", 
               tmp_path, error);
        ret = error;
        goto out_free_all;
    }
    
    /* 获取目标目录路径 */
    char *dir_name = kstrdup(final_path, GFP_KERNEL);
    if (!dir_name) {
        path_put(&old_path);
        ret = -ENOMEM;
        goto out_free_all;
    }
    
    char *last_slash = strrchr(dir_name, '/');
    if (last_slash)
        *last_slash = '\0';  /* 截断为目录名称 */
    else
        strcpy(dir_name, "/");  /* 根目录 */
    
    error = kern_path(dir_name, 0, &new_dir_path);
    if (error) {
        pr_err("ramfs_file_flush: 无法查找目标目录 %s, 错误码 %d\n", dir_name, error);
        path_put(&old_path);
        kfree(dir_name);
        ret = error;
        goto out_free_all;
    }
    kfree(dir_name);
    //printk(KERN_INFO "ramfs_file_flush: checkpoint3.1\n");
    /* 获取目标文件 dentry */
    const char *target_name = strrchr(final_path, '/');
    if (!target_name)
        target_name = final_path;
    else
        target_name++;  /* 跳过斜杠 */
    //printk(KERN_INFO "ramfs_file_flush: checkpoint3.2\n");
    //printk(KERN_INFO "ramfs_file_flush: target_name:-%s-, new_dir_path.dentry:-%s-\n", 
        //    target_name, new_dir_path.dentry->d_name.name);

    /* 在目标目录上获取 i_mutex 锁 */
    
    /* 在查找目标文件 dentry 前创建目标文件 */
    struct file *touch_file = filp_open(final_path, O_WRONLY | O_CREAT, 0644);
    if (!IS_ERR(touch_file)) {
        filp_close(touch_file, NULL);
    } else {
        pr_err("ramfs_file_flush: 创建目标文件失败: %ld\n", PTR_ERR(touch_file));
    }
    inode_lock(new_dir_path.dentry->d_inode);
    new_dentry = lookup_one_len(target_name, new_dir_path.dentry, 
                               strlen(target_name));

    /* 使用完后解锁目录 */
    inode_unlock(new_dir_path.dentry->d_inode);

    //printk(KERN_INFO "ramfs_file_flush: checkpoint3.3\n");
    if (IS_ERR(new_dentry)) {
        pr_err("ramfs_file_flush: 查找目标dentry失败\n");
        path_put(&old_path);
        path_put(&new_dir_path);
        ret = PTR_ERR(new_dentry);
        goto out_free_all;
    }
    
    /* 执行重命名 */
	struct renamedata rd = {
	    .old_mnt_userns = current_user_ns(),
	    .old_dir = d_inode(old_path.dentry->d_parent),
	    .old_dentry = old_path.dentry,
	    .new_mnt_userns = current_user_ns(),
	    .new_dir = d_inode(new_dir_path.dentry),
	    .new_dentry = new_dentry,
	    .flags = 0,
	};
	//printk(KERN_INFO "ramfs_file_flush: checkpoint4\n");
	inode_lock(d_inode(old_path.dentry->d_parent));
	if (old_path.dentry->d_parent != new_dir_path.dentry)
	    inode_lock_nested(d_inode(new_dir_path.dentry), I_MUTEX_PARENT);
	
	ret = vfs_rename(&rd);
	
	if (old_path.dentry->d_parent != new_dir_path.dentry)
	    inode_unlock(d_inode(new_dir_path.dentry));
	inode_unlock(d_inode(old_path.dentry->d_parent));
    
    dput(new_dentry);
    path_put(&old_path);
    path_put(&new_dir_path);
    
    if (ret) {
        pr_err("ramfs_file_flush: 重命名 %s -> %s 失败，错误码 %d\n", 
               tmp_path, final_path, ret);
    } else {
        pr_debug("ramfs_file_flush: 成功同步 %s\n", final_path);
    }
    //printk(KERN_INFO "ramfs_file_flush: checkpoint5\n");
    goto out_free_all;
    
out_close:
    //printk(KERN_INFO "ramfs_file_flush: checkpoint6\n");
    if (dest_file && !IS_ERR(dest_file))
        filp_close(dest_file, NULL);
    
out_free_all:
    //printk(KERN_INFO "ramfs_file_flush: checkpoint7\n");
    kfree(final_path);
    kfree(tmp_path);
    
out_free_path:
    //printk(KERN_INFO "ramfs_file_flush: checkpoint8\n");
    kfree(path_buf);
    
    return ret;
}

EXPORT_SYMBOL(ramfs_bind);
EXPORT_SYMBOL(ramfs_file_flush);