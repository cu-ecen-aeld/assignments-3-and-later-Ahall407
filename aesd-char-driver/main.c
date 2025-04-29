/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Alex Hall"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

// function declarations
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int aesd_init_module(void);
void aesd_cleanup_module(void);

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    struct aesd_dev *dev;

    // container_of gets aesd_dev from cdev embedded inside
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    filp->private_data = dev; // Link the file pointer to our aesd_dev

    return 0; // success
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    
    // Nothing dynamically allocated in open() so just return 0
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    size_t bytes_to_copy;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset);

    if (!entry) {
        retval = 0;
        goto out;
    }

    // Determine how much data to copy to userspace
    bytes_to_copy = entry->size - entry_offset;
    if (bytes_to_copy > count) {
        // Limit size to user's buffer
        bytes_to_copy = count;
    }

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        retval = -EFAULT; //Failed to copy to userspace
        goto out;
    }

    *f_pos += bytes_to_copy; // Advance file position by number of bytes read
    retval = bytes_to_copy;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *kbuff = NULL;
    char *newline_ptr = NULL;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    if (count == 0 || !buf){
        return -EINVAL;
    }

    // Allocate the temporary kernel buffer
    kbuff = kmalloc(count, GFP_KERNEL);
    if (!kbuff){
        return -ENOMEM;
    }

    // Copy the data being sent to kernel dev from Userspace
    if (copy_from_user(kbuff, buf, count)) {
        kfree(kbuff);
        return -EFAULT;
    }

    // Lock the device before sending data to shared structure
    if (mutex_lock_interruptible(&dev->lock)) {
        kfree(kbuff);
        return -ERESTARTSYS;
    }

    // Append new data to write_accumulator
    char *temp_accumulator = krealloc(dev->write_accumulator, dev->accumulator_size + count, GFP_KERNEL);
    if (!temp_accumulator) {
        mutex_unlock(&dev->lock);
        kfree(kbuff);
        return -ENOMEM;
    }

    memcpy(temp_accumulator + dev->accumulator_size, kbuff, count);
    dev->write_accumulator = temp_accumulator;
    dev->accumulator_size += count;

    // Check if the write_accumulator contains newline char \n
    newline_ptr = memchr(dev->write_accumulator, '\n', dev->accumulator_size);
    if (newline_ptr != NULL) {
        // Successfully recieved the full command and store to circular bufer
        struct aesd_buffer_entry new_entry;
        const char *old_buffptr;

        new_entry.buffptr = dev->write_accumulator;
        new_entry.size = dev->accumulator_size;

        old_buffptr = aesd_circular_buffer_add_entry(&dev->circular_buffer, &new_entry);
        if (old_buffptr) {
            // free memory associated with overwritten entry
            kfree(old_buffptr);
        }

        // reset accumulator
        dev->write_accumulator = NULL;
        dev->accumulator_size = 0;
    }

    mutex_unlock(&dev->lock); // unlock shared device
    kfree(kbuff); // free temp kernel buffer
    retval = count; // return number of bytes successfully written
    *f_pos += retval; // advance file position by number of bytes written

    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Calculate total size of valid buffer content
    size_t total_size = aesd_circular_buffer_size(&dev->circular_buffer);

    // Use vfs_llseek to compute new position
    new_pos = vfs_llseek(filp, offset, whence);

    // Clamp new position to valid range [0, total_size]
    if (new_pos < 0)
        new_pos = 0;
    else if (new_pos > total_size)
        new_pos = total_size;

    filp->f_pos = new_pos;  // Update file position

    mutex_unlock(&dev->lock);

    return new_pos;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_seekto seekto;
    struct aesd_dev *dev = filp->private_data;
    ssize_t offset = 0;
    uint8_t index;

    // Check that the cmd corresponds to the correct ioctl command number
    if (cmd != AESDCHAR_IOCSEEKTO)
        return -ENOTTY;

    // Copy the aesd_seekto struct with write_cmd and write_cmd_offset from user space
    if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)))
        return -EFAULT;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Validate index and offset
    // Ensure write_cmd within bounds of buffer size
    // Ensure the target buffer exists
    // Ensure write_cmd_offset is not beyond the commands size
    if (seekto.write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ||
        dev->circular_buffer.entry[seekto.write_cmd].buffptr == NULL ||
        seekto.write_cmd_offset > dev->circular_buffer.entry[seekto.write_cmd].size)
    {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    // Compute the byte offset from start of buffer
    // Add up sizes of all prior commands
    for (index = 0; index < seekto.write_cmd; index++) {
        offset += dev->circular_buffer.entry[index].size;
    }
    // Add offset to get absolute position
    offset += seekto.write_cmd_offset;

    // Update f_pos so read/writes correctly aligned
    filp->f_pos = offset;

    mutex_unlock(&dev->lock);
    return 0;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}


int aesd_init_module(void)
{
    dev_t dev = 0; // device number
    int result;

    // Allocate major/minor dynamically
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     *  Initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    // Remove cdev from the kernel
    cdev_del(&aesd_device.cdev);

    /**
     *  Cleanup AESD specific poritions here as necessary
     */
    struct aesd_buffer_entry *entry;
    uint8_t index;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
            entry->buffptr = NULL;
        }
    }

    // Unregister the device number
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
