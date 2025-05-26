#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/acpi.h>

/* 自定义ACPI表结构 */
struct my_acpi_table {
    struct acpi_table_header header;
    u32 my_data1;
    u64 my_data2;
    char my_string[64];
};

static struct kobject *mytb_kobj;

/* 回调函数，用于读取MYTB表的MyString字段 */
static ssize_t mytb_show(struct kobject *kobj,
                         struct kobj_attribute *attr, char *buf)
{
    struct acpi_table_header *table = NULL;
    struct my_acpi_table *mytb = NULL;
    acpi_status status;
    ssize_t len = 0;

    /* 获取MYTB表 */
    status = acpi_get_table("MYTB", 0, &table);
    if (ACPI_FAILURE(status)) {
        pr_err("Failed to get ACPI MYTB table\n");
        return -ENODEV;
    }

    /* 转换为我们的自定义结构 */
    mytb = (struct my_acpi_table *)table;
    
    /* 拷贝MyString字段到输出缓冲区 */
    len = strnlen(mytb->my_string, 64);
    if (len > 0)
        memcpy(buf, mytb->my_string, len);
    
    /* 确保字符串以NULL结尾 */
    if (len < PAGE_SIZE)
        buf[len] = '\0';
    
    return len;
}

/* 定义sysfs属性 */
static struct kobj_attribute mytb_attribute =
    __ATTR(mytb_string, 0444, mytb_show, NULL); /* 只读权限 */

static int __init acpi_mytb_init(void)
{
    int retval = 0;
    struct acpi_table_header *table = NULL;
    acpi_status status;

    /* 检查MYTB表是否存在 */
    status = acpi_get_table("MYTB", 0, &table);
    if (ACPI_FAILURE(status)) {
        pr_info("ACPI MYTB table not found, not creating sysfs entry\n");
        return -ENODEV;
    }

    /* 创建/sys/kernel/mytb目录 */
    mytb_kobj = kobject_create_and_add("mytb", kernel_kobj);
    if (!mytb_kobj)
        return -ENOMEM;

    /* 创建/sys/kernel/mytb/mytb_string文件 */
    retval = sysfs_create_file(mytb_kobj, &mytb_attribute.attr);
    if (retval) {
        pr_err("Failed to create MYTB sysfs file\n");
        kobject_put(mytb_kobj);
        return retval;
    }

    pr_info("MYTB table module initialized. Access at /sys/kernel/mytb/mytb_string\n");
    return 0;
}

static void __exit acpi_mytb_exit(void)
{
    kobject_put(mytb_kobj);
    pr_info("MYTB table module unloaded\n");
}

module_init(acpi_mytb_init);
module_exit(acpi_mytb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACPI MYTB table sysfs interface");
MODULE_AUTHOR("Your Name");