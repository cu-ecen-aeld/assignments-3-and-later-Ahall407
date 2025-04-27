#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aesd-circular-buffer.h"

void write_circular_buffer_packet(struct aesd_circular_buffer *buffer, const char *write_string) {
    struct aesd_buffer_entry entry;
    const char *rtnptr;
    entry.buffptr = write_string;
    entry.size = strlen(write_string);
    rtnptr = aesd_circular_buffer_add_entry(buffer, &entry);
    if (rtnptr) {
        free((void *)rtnptr);
    }
}

void test_multiple_partial_writes(struct aesd_circular_buffer *buffer) {
    // Simulate receiving partial chunks like over a network or userspace write() calls

    // "fir" "st\nsec" "ond\nthi" "rd\n"
    char *writes[] = {
        strdup("fir"),      // not complete
        strdup("st\nsec"),  // complete "first\n", partial "sec"
        strdup("ond\nthi"), // completes "second\n", partial "thi"
        strdup("rd\n")      // completes "third\n"
    };
    size_t writes_count = sizeof(writes) / sizeof(writes[0]);

    printf("[Test] Writing multiple partial command pieces...\n");

    for (size_t i = 0; i < writes_count; i++) {
        write_circular_buffer_packet(buffer, writes[i]);
        free(writes[i]);
    }
}

static void test_multiple_reads()
{
    struct aesd_circular_buffer buffer;
    aesd_circular_buffer_init(&buffer);

    printf("[Test] Writing multiple partial command pieces...\n");

    write_circular_buffer_packet(&buffer, strdup("st"));
    write_circular_buffer_packet(&buffer, strdup("art\n"));
    write_circular_buffer_packet(&buffer, strdup("second\n"));
    write_circular_buffer_packet(&buffer, strdup("third\n"));

    printf("[Test] Reading back assembled commands...\n");

    size_t offset = 0;
    size_t entry_offset = 0;
    const struct aesd_buffer_entry *entry;
    size_t read_bytes;

    while (true)
    {
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(&buffer, offset, &entry_offset);
        if (!entry) {
            printf("End of buffer reached at offset %zu\n", offset);
            break;
        }

        // Print from the current offset inside the entry
        const char *start_ptr = entry->buffptr + entry_offset;
        read_bytes = entry->size - entry_offset; // how much left to read in this entry

        printf("Read at offset %zu: ", offset);
        fwrite(start_ptr, 1, read_bytes, stdout);
        printf("\n");

        offset += read_bytes; // move forward
    }
}

int main() {
    struct aesd_circular_buffer test_buffer;
    aesd_circular_buffer_init(&test_buffer);

    test_multiple_partial_writes(&test_buffer);
    test_multiple_reads(&test_buffer);

    return 0;
}