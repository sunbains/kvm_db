/*
 * wal_test.c - Test program for WAL device driver
 * 
 * This program tests the functionality of the WAL driver by:
 * - Opening both character and block devices
 * - Testing read and write operations
 * - Using ioctl commands
 * - Displaying statistics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <getopt.h>
#include <ctype.h>
#include <assert.h>

#include "wal_driver.h"

#define BUFFER_SIZE 1024
#define TEST_DATA "Hello from WAL test program!"

/* Function prototypes */
static int test_character_device(void);
static int test_block_device(void);
static int test_ioctl_commands(void);
static void print_usage(const char *program_name);
static void print_device_info(void);

int main(int argc, char *argv[])
{
    int opt;
    int test_char = 0, test_block = 0, test_ioctl = 0, show_info = 0;
    int all_tests = 0;
    
    printf("WAL Driver Test Program v1.0\n");
    printf("============================\n\n");
    
    /* Parse command line options */
    while ((opt = getopt(argc, argv, "cbiahe")) != -1) {
        switch (opt) {
        case 'c':
            test_char = 1;
            break;
        case 'b':
            test_block = 1;
            break;
        case 'i':
            test_ioctl = 1;
            break;
        case 'a':
            all_tests = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'e':
            show_info = 1;
            break;
        default:
            fprintf(stderr, "Unknown option. Use -h for help.\n");
            return 1;
        }
    }
    
    /* If no specific tests requested, run all tests */
    if (!test_char && !test_block && !test_ioctl && !show_info) {
        all_tests = 1;
    }
    
    if (all_tests) {
        test_char = test_block = test_ioctl = show_info = 1;
    }
    
    /* Show device information */
    if (show_info) {
        print_device_info();
    }
    
    /* Run requested tests */
    if (test_char) {
        printf("Testing character device (/dev/rwal)...\n");
        if (test_character_device() != 0) {
            printf("Character device test failed.\n");
        } else {
            printf("Character device test completed successfully.\n");
        }
        printf("\n");
    }
    
    if (test_block) {
        printf("Testing block device (/dev/wal)...\n");
        if (test_block_device() != 0) {
            printf("Block device test failed.\n");
        } else {
            printf("Block device test completed successfully.\n");
        }
        printf("\n");
    }
    
    if (test_ioctl) {
        printf("Testing ioctl commands...\n");
        if (test_ioctl_commands() != 0) {
            printf("IOCTL test failed.\n");
        } else {
            printf("IOCTL test completed successfully.\n");
        }
        printf("\n");
    }
    
    printf("All requested tests completed.\n");
    printf("Check dmesg for kernel driver messages.\n");
    
    return 0;
}

static int test_character_device(void)
{
    int fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    
    /* Open character device */
    fd = open("/dev/rwal", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/rwal");
        return -1;
    }
    
    printf("  Character device opened successfully (fd=%d)\n", fd);
    
    /* Test write operation */
    printf("  Writing test data: \"%s\"\n", TEST_DATA);
    bytes_written = write(fd, TEST_DATA, strlen(TEST_DATA));
    if (bytes_written < 0) {
        perror("  Failed to write to character device");
        close(fd);
        return -1;
    }
    printf("  Wrote %zd bytes to character device\n", bytes_written);
    
    /* Reset file position for reading */
    lseek(fd, 0, SEEK_SET);
    
    /* Test read operation */
    printf("  Reading from character device...\n");
    memset(buffer, 0, sizeof(buffer));
    bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("  Failed to read from character device");
        close(fd);
        return -1;
    }
    
    buffer[bytes_read] = '\0';  /* Null-terminate */
    printf("  Read %zd bytes: \"%s\"\n", bytes_read, buffer);
    
    /* Test multiple small reads */
    printf("  Testing multiple small reads...\n");
    lseek(fd, 0, SEEK_SET);
    for (int i = 0; i < 3; i++) {
        memset(buffer, 0, 10);
        bytes_read = read(fd, buffer, 5);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("    Read %zd: \"%s\"\n", bytes_read, buffer);
        }
    }
    
    close(fd);
    printf("  Character device closed\n");
    
    return 0;
}

static int test_block_device(void)
{
    int fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    const char *block_test_data = "Block device test data - 512 bytes block";
    
    /* Open block device */
    fd = open("/dev/wal", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/wal");
        return -1;
    }
    
    printf("  Block device opened successfully (fd=%d)\n", fd);
    
    /* Test write operation */
    printf("  Writing test data: \"%s\"\n", block_test_data);
    bytes_written = write(fd, block_test_data, strlen(block_test_data));
    if (bytes_written < 0) {
        perror("  Failed to write to block device");
        close(fd);
        return -1;
    }
    printf("  Wrote %zd bytes to block device\n", bytes_written);
    
    /* Test block-aligned write */
    printf("  Testing 512-byte block write...\n");
    memset(buffer, 'A', 512);
    snprintf(buffer, 50, "Block-aligned test data %d", 12345);
    buffer[49] = 'A';  /* Restore the pattern */
    
    bytes_written = write(fd, buffer, 512);
    if (bytes_written < 0) {
        perror("  Failed to write 512-byte block");
        close(fd);
        return -1;
    }
    printf("  Wrote %zd bytes (block-aligned)\n", bytes_written);
    
    /* Reset file position for reading */
    lseek(fd, 0, SEEK_SET);
    
    /* Test read operation */
    printf("  Reading from block device...\n");
    memset(buffer, 0, sizeof(buffer));
    bytes_read = read(fd, buffer, 512);
    if (bytes_read < 0) {
        perror("  Failed to read from block device");
        close(fd);
        return -1;
    }
    
    printf("  Read %zd bytes from block device\n", bytes_read);
    printf("  First 32 bytes: ");
    for (int i = 0; i < 32 && i < bytes_read; i++) {
        if (isprint(buffer[i])) {
            printf("%c", buffer[i]);
        } else {
            printf(".");
        }
    }
    printf("\n");
    
    /* Test reading from different offset */
    printf("  Testing read from offset 256...\n");
    lseek(fd, 256, SEEK_SET);
    bytes_read = read(fd, buffer, 128);
    if (bytes_read > 0) {
        printf("  Read %zd bytes from offset 256\n", bytes_read);
    }
    
    close(fd);
    printf("  Block device closed\n");
    
    return 0;
}

static int test_ioctl_commands(void)
{
    int fd;
    struct wal_status status;
    enum wal_mode new_mode;
    
    /* Open character device for ioctl testing */
    fd = open("/dev/rwal", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/rwal for ioctl");
        return -1;
    }
    
    printf("  Testing ioctl commands...\n");
    
    /* Get current status */
    printf("  Getting current status...\n");
    if (ioctl(fd, WAL_IOC_GET_STATUS, &status) < 0) {
        perror("  Failed to get status");
        close(fd);
        return -1;
    }
    
    printf("  Current statistics:\n");
    printf("    Character reads:  %u\n", status.char_read_count);
    printf("    Character writes: %u\n", status.char_write_count);
    printf("    Block reads:      %u\n", status.block_read_count);
    printf("    Block writes:     %u\n", status.block_write_count);
    printf("    Total bytes read: %u\n", status.total_bytes_read);
    printf("    Total bytes written: %u\n", status.total_bytes_written);
    printf("    Current mode:     %d\n", status.current_mode);
    
    /* Test mode change */
    printf("  Changing mode to DEBUG...\n");
    new_mode = WAL_MODE_DEBUG;
    if (ioctl(fd, WAL_IOC_SET_MODE, &new_mode) < 0) {
        perror("  Failed to set mode");
        close(fd);
        return -1;
    }
    printf("  Mode changed to DEBUG\n");
    
    /* Perform a small operation to see debug output */
    printf("  Performing operation in DEBUG mode...\n");
    int written_bytes = write(fd, "Debug test", 10);
    assert(written_bytes == 10);
    
    /* Change back to normal mode */
    printf("  Changing mode back to NORMAL...\n");
    new_mode = WAL_MODE_NORMAL;
    if (ioctl(fd, WAL_IOC_SET_MODE, &new_mode) < 0) {
        perror("  Failed to set mode back to normal");
        close(fd);
        return -1;
    }
    
    /* Test reset command */
    printf("  Testing statistics reset...\n");
    if (ioctl(fd, WAL_IOC_RESET) < 0) {
        perror("  Failed to reset statistics");
        close(fd);
        return -1;
    }
    printf("  Statistics reset successfully\n");
    
    /* Get status after reset */
    if (ioctl(fd, WAL_IOC_GET_STATUS, &status) < 0) {
        perror("  Failed to get status after reset");
        close(fd);
        return -1;
    }
    
    printf("  Statistics after reset:\n");
    printf("    Character reads:  %u\n", status.char_read_count);
    printf("    Character writes: %u\n", status.char_write_count);
    printf("    Block reads:      %u\n", status.block_read_count);
    printf("    Block writes:     %u\n", status.block_write_count);
    
    close(fd);
    return 0;
}

static void print_device_info(void)
{
    struct stat st;
    
    printf("Device Information:\n");
    printf("-------------------\n");
    
    /* Check character device */
    if (stat("/dev/rwal", &st) == 0) {
        printf("Character device /dev/rwal:\n");
        printf("  Type: %s\n", S_ISCHR(st.st_mode) ? "Character device" : "Not a character device");
        printf("  Major: %d, Minor: %d\n", major(st.st_rdev), minor(st.st_rdev));
        printf("  Permissions: %o\n", st.st_mode & 0777);
        printf("  Accessible: %s\n", access("/dev/rwal", R_OK | W_OK) == 0 ? "Yes" : "No");
    } else {
        printf("Character device /dev/rwal: Not found\n");
    }
    
    /* Check block device */
    if (stat("/dev/wal", &st) == 0) {
        printf("Block device /dev/wal:\n");
        printf("  Type: %s\n", S_ISBLK(st.st_mode) ? "Block device" : "Not a block device");
        printf("  Major: %d, Minor: %d\n", major(st.st_rdev), minor(st.st_rdev));
        printf("  Permissions: %o\n", st.st_mode & 0777);
        printf("  Accessible: %s\n", access("/dev/wal", R_OK | W_OK) == 0 ? "Yes" : "No");
    } else {
        printf("Block device /dev/wal: Not found\n");
    }
    
    /* Check proc entry */
    if (access("/proc/wal_driver", R_OK) == 0) {
        printf("Proc entry /proc/wal_driver: Available\n");
        printf("  Content:\n");
        FILE *proc_file = fopen("/proc/wal_driver", "r");
        if (proc_file) {
            char line[256];
            while (fgets(line, sizeof(line), proc_file)) {
                printf("    %s", line);
            }
            fclose(proc_file);
        }
    } else {
        printf("Proc entry /proc/wal_driver: Not available\n");
    }
    
    printf("\n");
}

static void print_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -c    Test character device only\n");
    printf("  -b    Test block device only\n");
    printf("  -i    Test ioctl commands only\n");
    printf("  -e    Show device information\n");
    printf("  -a    Run all tests (default if no options given)\n");
    printf("  -h    Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s          # Run all tests\n", program_name);
    printf("  %s -c       # Test character device only\n", program_name);
    printf("  %s -b -i    # Test block device and ioctl commands\n", program_name);
    printf("  %s -e       # Show device information only\n", program_name);
    printf("\n");
    printf("Note: Make sure the WAL driver module is loaded before running tests.\n");
    printf("Use 'sudo modprobe wal_driver' or 'make load' to load the driver.\n");
}

