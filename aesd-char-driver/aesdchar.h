/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */
#include "aesd-circular-buffer.h"
#include <linux/cdev.h>
#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    /**
     * DONE: Add structure(s) and locks needed to complete assignment requirements
     */
    struct cdev cdev;                             // Char device structure
    struct aesd_circular_buffer circular_buffer;  // circular buffer for aesd-circular-buffer
    struct mutex lock;                            // lock for concurrrent access

    char *write_accumulator;                      // buffer for partial writes
    size_t accumulator_size;                      // Size of accumuatated data
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
