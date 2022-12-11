#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");

#define MAX_LEN 512

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;
static struct debugfs_blob_wrapper *result_wrapper;
static char* result;
static char* tmp_buffer;

static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
        pid_t input_pid;
        char result_temp[1000];
        int i;
        
        tmp_buffer = (char *)kmalloc(MAX_LEN * sizeof(char), GFP_KERNEL);

        if (copy_from_user(tmp_buffer, user_buffer, length)) {
            return -EFAULT;
        }

        sscanf(tmp_buffer, "%u", &input_pid);

        curr = pid_task(find_vpid(input_pid), PIDTYPE_PID);
        if (curr == NULL) {
            return -EINVAL;
        }

        for (i = 0; i < 1000; i++){
                result[i] = 0;
                result_temp[i] = 0;
        }
        while (1) {
            strcpy(result_temp, result);
            sprintf(result, "%s (%d)\n%s", curr->comm, curr->pid, result_temp);
            if (curr->pid == 1){
                break;
            }
            curr = curr->real_parent;
        }

        kfree(tmp_buffer);

        return length;
}

static const struct file_operations dbfs_fops = {
        .write = write_pid_to_input,
};

static int __init dbfs_module_init(void)
{
        // Implement init module code
        dir = debugfs_create_dir("ptree", NULL);
        if (!dir) {
                printk("Cannot create ptree dir\n");
                return -1;
        }

        inputdir = debugfs_create_file("input", S_IRWXU, dir, NULL, &dbfs_fops);

        // Allocated space for debugfs_blob_wrapper
        result_wrapper = (struct debugfs_blob_wrapper*)kmalloc(sizeof(struct debugfs_blob_wrapper), GFP_KERNEL);

        // Allocated space for output string
        result = (char*)kmalloc(1000 * sizeof(char), GFP_KERNEL);

        // Setting blob_wrapper
        result_wrapper->data = (void*)result;
        result_wrapper->size = (unsigned long)(1000 * sizeof(char));

        ptreedir = debugfs_create_blob("ptree", S_IRWXU, dir, result_wrapper);

	printk("dbfs_ptree module initialize done\n");

        return 0;
}

static void __exit dbfs_module_exit(void)
{
    debugfs_remove_recursive(dir);
    kfree(result_wrapper);
    kfree(result);
    printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);