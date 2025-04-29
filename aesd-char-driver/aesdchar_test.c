#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "aesd_ioctl.h"

#define DEVICE_PATH "/dev/aesdchar"
#define BUFFER_SIZE 1024

void check_result(const char *desc, const char *expected, const char *actual, ssize_t read_len) {
    printf("%s\n", desc);
    printf("Expected: \"%s\"\n", expected);
    printf("Actual  : \"%.*s\"\n", (int)read_len, actual);
    if (strncmp(expected, actual, strlen(expected)) == 0) {
        printf("[PASS]\n\n");
    } else {
        printf("[FAIL]\n\n");
    }
}

int main() {
    int fd;
    char read_buf[BUFFER_SIZE];
    ssize_t bytes_read;
    struct aesd_seekto seekto;

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    // Clean start: write known entries
    write(fd, "swrite1\n", 8);
    write(fd, "swrite2\n", 8);
    write(fd, "swrite3\n", 8);
    write(fd, "swrite4\n", 8);
    write(fd, "swrite5\n", 8);

    printf("Finished writing known entries.\n\n");

    // Test 1: Seek into write_cmd 0, offset 2 ("swrite1" -> 'r')
    seekto.write_cmd = 0;
    seekto.write_cmd_offset = 2;
    if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) == -1) {
        perror("IOCTL failed");
        close(fd);
        return EXIT_FAILURE;
    }
    lseek(fd, 0, SEEK_CUR);  // Sync VFS and internal f_pos if needed

    bytes_read = read(fd, read_buf, sizeof(read_buf));
    check_result("Test 1: Seek to write_cmd 0 offset 2",
                 "rite1\nswrite2\nswrite3\nswrite4\nswrite5\n",
                 read_buf, bytes_read);

    // Test 2: Seek into write_cmd 4, offset 1 ("swrite5" -> 'w')
    seekto.write_cmd = 4;
    seekto.write_cmd_offset = 1;
    if (ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto) == -1) {
        perror("IOCTL failed");
        close(fd);
        return EXIT_FAILURE;
    }
    lseek(fd, 0, SEEK_CUR);  // Again sync if needed

    bytes_read = read(fd, read_buf, sizeof(read_buf));
    check_result("Test 2: Seek to write_cmd 4 offset 1",
                 "write5\n",
                 read_buf, bytes_read);

    close(fd);
    return EXIT_SUCCESS;
}