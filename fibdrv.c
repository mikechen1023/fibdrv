#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include "bignum.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Nationalz Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 150
#define BUF_SIZE 256

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

long int str_size(char *str)
{
    long int retsize = 0;
    char *s = str;

    while (*s != '\0') {
        retsize++;
        s++;
    }

    return retsize;
}

void str_cpy(char *dst, char *src, int size)
{
    for (int i = 0; i < size; i++)
        *(dst + i) = *(src + i);

    *(dst + size) = '\0';
}

void str_swap(char *a, char *b)
{
    if (*a != *b) {
        *a = *a ^ *b;
        *b = *a ^ *b;
        *a = *a ^ *b;
    }
}

void reverse(char *str, int size)
{
    for (int i = 0; i < size / 2; i++) {
        str_swap(&str[i], &str[(size - 1) - i]);
    }
}


void sum(char *buf, char *str1, char *str2)
{
    // printk("Orig fb_0 = %s\n", str1);
    // printk("Orig fb_1 = %s\n", str2);

    // first reverse
    reverse(str1, str_size(str1));
    reverse(str2, str_size(str2));

    // sum
    int str1_size = str_size(str1);
    int str2_size = str_size(str2);
    int carry = 0, sum, i = 0;

    for (i = 0; i < str1_size; i++) {
        sum = str1[i] - '0' + str2[i] - '0' + carry;
        buf[i] = sum % 10 + '0';
        carry = sum / 10;
    }


    // printk("buf = %s\n", buf);

    for (i = str1_size; i < str2_size; i++) {
        sum = str2[i] - '0' + carry;
        buf[i] = sum % 10 + '0';
        carry = sum / 10;
    }

    if (carry)
        buf[i++] = '1';

    buf[i] = '\0';

    // printk("After sum  = %s\n", buf);

    // second reverse
    reverse(buf, i);
    reverse(str1, str_size(str1));
    reverse(str2, str_size(str2));

    // printk("After fb_0 = %s\n", str1);
    // printk("After fb_1 = %s\n", str2);
    // printk("After reverse  = %s\n", buf);
}

static long int fib_sequence(long long k, char *buf)
{
    char tmpBuf[BUF_SIZE];
    char fb_0[BUF_SIZE] = "0", fb_1[BUF_SIZE] = "1";
    if (!k || k == 1) {
        buf[0] = '0' + k;
        buf[1] = '\0';

        // copy_to_user(buf, tmpBuf, 2);
        return 1;
    }

    // printk("fb_0 = %s\n", fb_0);
    // printk("fb_1 = %s\n", fb_1);

    for (long int i = 1; i < k; i++) {
        sum(tmpBuf, fb_0, fb_1);
        str_cpy(fb_0, fb_1, str_size(fb_1));
        str_cpy(fb_1, tmpBuf, str_size(tmpBuf));
    }

    long int retSize = str_size(tmpBuf);
    str_cpy(buf, tmpBuf, str_size(tmpBuf));
    // copy_to_user(buf, fb_0, retSize + 1);

    // copy_to_user(bufm tmpBuf, size);
    return retSize;
}



// static long long fib_sequence(long long k)
// {
//     /* FIXME: C99 variable-length array (VLA) is not allowed in Linux kernel.
//     */ long long f[k + 2];

//     f[0] = 0;
//     f[1] = 1;

//     for (int i = 2; i <= k; i++) {
//         f[i] = f[i - 1] + f[i - 2];
//     }

//     return f[k];
// }

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    printk("================= %lld ===================", *offset);
    char result[BUF_SIZE];
    long long len = fib_sequence(*offset, result);
    printk("final result = %s\n",
           result);  // if use demsg, have to add "\n", otherwise the last line
                     // will disappear
    size_t left = copy_to_user(buf, result, len);
    return left;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
