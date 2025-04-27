/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#else
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#endif


#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{

    size_t cumulative_offset = 0;
    uint8_t index = buffer->out_offs;
    uint8_t count;

    if (buffer->full) {
        count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
    else if (buffer->in_offs >= buffer->out_offs){
        count = buffer->in_offs - buffer->out_offs;
    }
    else {
        count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs + buffer->in_offs;
    }

    for (uint8_t i =0; i < count; i++){
        struct aesd_buffer_entry *entry = &buffer->entry[index];
        if (char_offset < cumulative_offset + entry->size) {
            *entry_offset_byte_rtn = char_offset - cumulative_offset;
            return entry;
        }
        cumulative_offset += entry->size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // return NULL if out of range
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
     * Adds an entry to the circular buffer.
     * If an old entry is overwritten, returns the buffptr of that entry
     * so the caller can free associated memory if needed.
     * 
     * @return pointer to overwritten entry's buffptr, or NULL if none.
     */
    const char *old_buffptr = NULL;

    // If buffer is full, store the old pointer to free it later
    if (buffer->full) {
        old_buffptr = buffer->entry[buffer->out_offs].buffptr;
    }

    // Allocate memory for new string
    char *new_buff = kmalloc(add_entry->size, GFP_KERNEL);
    if (!new_buff) {
        // Allocation failed
        printk(KERN_ERR "kmalloc failed in aesd-circular-buffer\n");
        return -ENOMEM;
    }
    memcpy(new_buff, add_entry->buffptr, add_entry->size);

    // Replace entry at in_offs
    buffer->entry[buffer->in_offs].buffptr = new_buff;
    buffer->entry[buffer->in_offs].size = add_entry->size;

    buffer->entry[buffer->in_offs] = *add_entry;

    // Advance in_offs
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    if (buffer->full) {
        // Also move out_offs to next (drop oldest entry)
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // If after the move, in == out, buffer is full
    buffer->full = (buffer->in_offs == buffer->out_offs);

    return old_buffptr;

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
