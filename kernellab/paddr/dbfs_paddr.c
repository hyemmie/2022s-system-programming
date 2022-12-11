#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

MODULE_LICENSE("GPL");

#define MAX_LEN 512
static struct dentry *dir, *output;
static struct task_struct *task;

unsigned char kernel_buf[MAX_LEN];
unsigned char output_buf[MAX_LEN];

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
    /* page table read */
    struct mm_struct *mm;
    pgd_t *pgdp;
    p4d_t *p4dp;
    pud_t *pudp;
    pmd_t *pmdp;
    pte_t *ptep;
    phys_addr_t pfn;
    pid_t input_pid;
    
    unsigned long vaddr = 0;
    ssize_t total_len = 0;
    int i, real_length = 0;
    long ret, remainder, divisor, mul = 0;
    unsigned long base = (1 << 8);
    
    /* initialize the buffers */
    memset(kernel_buf, 0, MAX_LEN);
    memset(output_buf, 0, MAX_LEN);
    
    real_length = (length < MAX_LEN) ? length : MAX_LEN;

    if (copy_from_user(kernel_buf, user_buffer, real_length))
        return -EFAULT;

    /* for the input pid, 2^8 should be multiplied */
    input_pid = (int)(kernel_buf[0]) + (int)(kernel_buf[1])*(base)
        + (int)(kernel_buf[2]*(base << 8) + (int)(kernel_buf[3]*(base <<16)));

    /* caculate the virtual address */
    for (i = 0; i < 6; i++) {
        ret = 1;
        remainder = i & 1;
        divisor = i;
        mul = base;

        while(divisor) {
            ret = (remainder) ? ret*mul : ret;
            mul *= mul;
            divisor >>= 1;
            remainder = divisor & 1;
        }
        vaddr += (long)(kernel_buf[8+i]) * ret;
    }
    
    /* find the task_struct */
    task = pid_task(find_get_pid(input_pid), PIDTYPE_PID);
    if(!task)
        return -EINVAL;

    /* using the page table for the physical address */
    mm = task->mm;
    pgdp = pgd_offset(mm, vaddr);
    if(pgd_none(*pgdp) || pgd_bad(*pgdp)) {
        printk("pgd offset is invalid\n");
        return -EFAULT;
    }
    p4dp = p4d_offset(pgdp, vaddr);
    if(p4d_none(*p4dp) || p4d_bad(*p4dp)) {
        printk("p4d offset is invalid\n");
        return -EFAULT;
    }
    pudp = pud_offset(p4dp, vaddr);
    if(pud_none(*pudp) || pud_bad(*pudp)) {
        printk("pud offset is invalid\n");
        return -EFAULT;
    }
    pmdp = pmd_offset(pudp, vaddr);
    if(pmd_none(*pmdp) || pmd_bad(*pmdp)) {
        printk("pmd offset is invalid\n");
        return -EFAULT;
    }
    ptep = pte_offset_kernel(pmdp, vaddr);
    if(pte_none(*ptep) || !pte_present(*ptep)) {
        printk("pte offset is invalid\n");
        return -EFAULT;
    }
    pfn = pte_pfn(*ptep);
    
    /* copy the physical address to the output buffer */
    for(i = 0; i < 16; i++)
        output_buf[i] = kernel_buf[i];
    
    if(real_length >= 16)
        output_buf[16] = kernel_buf[16];
    
    if(real_length >= 17) {
        output_buf[17] = (kernel_buf[9] % 16) + ((pfn % 16)<<4);
        pfn >>= 4;
    }
    for(i = 18; i < real_length; i++) {
        output_buf[i] = (pfn % base);
        pfn >>= 8;
    }
    output_buf[real_length] = '\0';
    
    /* read the output from the output buffer to user buffer */
    total_len = simple_read_from_buffer(user_buffer, length, \
                                position, output_buf, real_length);
    return total_len;
}

static const struct file_operations dbfs_fops = {
        // Mapping file operations with your functions
        .read = read_output,
};

static int __init dbfs_module_init(void)
{

    /* init module - creating the directory */
    dir = debugfs_create_dir("paddr", NULL);
    if (!dir) {
        printk("Cannot create paddr dir\n");
        return -1;
    }
    /* output - for the file read operation */
    output = debugfs_create_file("output", S_IRUSR, dir, NULL, &dbfs_fops);
    
    if (!output) {
        printk("Cannot create dbfs_paddr output file\n");
        return -1;
    }
    printk("dbfs_paddr module initialize done\n");
    return 0;
}

static void __exit dbfs_module_exit(void)
{
    debugfs_remove_recursive(dir);
    printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
