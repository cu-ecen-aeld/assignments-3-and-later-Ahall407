#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aesd-circular-buffer.h"
#include "unity.h"


void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

static void write_circular_buffer_packet(struct aesd_circular_buffer *buffer,
    const char *write_string)
{
    struct aesd_buffer_entry entry;
    const char *rtnptr;
    char *copy = strdup(write_string);  // <--- malloc'd copy!
    const char *start_out_ptr = buffer->entry[buffer->out_offs].buffptr;
    bool buffer_was_full = buffer->full;

    entry.buffptr = copy;
    entry.size = strlen(copy);

    rtnptr = aesd_circular_buffer_add_entry(buffer, &entry);

    if (buffer_was_full) {
        TEST_ASSERT_EQUAL_PTR_MESSAGE(start_out_ptr, rtnptr, "Expect pointer to overwritten entry when full.");
        free((void *)rtnptr);
    } 
    else {
        TEST_ASSERT_NULL_MESSAGE(rtnptr, "Expect NULL when buffer not full.");
    }
}

void test_fill_and_overwrite_buffer(void)
{
    struct aesd_circular_buffer test_buffer = {0};
    char temp[64];

    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED + 3; i++) {
        snprintf(temp, sizeof(temp), "command %d\n", i);
        write_circular_buffer_packet(&test_buffer, temp);
    }

    // Final check: all entries should contain some data
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(test_buffer.entry[i].buffptr, "Expected buffer entry not to be NULL.");
    }

    // Final cleanup
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        free((void *)test_buffer.entry[i].buffptr);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fill_and_overwrite_buffer);
    return UNITY_END();
}