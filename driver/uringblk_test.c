/*
 * uringblk_test.c - Test program for uringblk io_uring-first block driver
 *
 * This program tests the uringblk driver functionality including:
 * - Basic I/O operations with io_uring
 * - URING_CMD admin interface
 * - Polling and fixed buffer support
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <liburing.h>
#include <time.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

/* Userspace definitions from uringblk_driver.h */
#define URINGBLK_ABI_MAJOR  1
#define URINGBLK_ABI_MINOR  0

enum uringblk_ucmd {
    URINGBLK_UCMD_IDENTIFY      = 0x01,
    URINGBLK_UCMD_GET_LIMITS    = 0x02,
    URINGBLK_UCMD_GET_FEATURES  = 0x03,
    URINGBLK_UCMD_SET_FEATURES  = 0x04,
    URINGBLK_UCMD_GET_GEOMETRY  = 0x05,
    URINGBLK_UCMD_GET_STATS     = 0x06,
    URINGBLK_UCMD_ZONE_MGMT     = 0x10,
    URINGBLK_UCMD_FIRMWARE_OP   = 0x20,
};

struct uringblk_ucmd_hdr {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t opcode;           /* enum uringblk_ucmd */
    uint16_t flags;            /* reserved */
    uint32_t payload_len;
    /* payload follows */
} __attribute__((packed));

struct uringblk_identify {
    uint8_t  model[40];
    uint8_t  firmware[16];
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint64_t capacity_sectors;
    uint64_t features_bitmap;
    uint32_t queue_count;
    uint32_t queue_depth;
    uint32_t max_segments;
    uint32_t max_segment_size;
    uint32_t dma_alignment;
    uint32_t io_min;
    uint32_t io_opt;
    uint32_t discard_granularity;
    uint64_t discard_max_bytes;
} __attribute__((packed));

struct uringblk_stats {
    uint64_t read_ops;
    uint64_t write_ops;
    uint64_t flush_ops;
    uint64_t discard_ops;
    uint64_t read_sectors;
    uint64_t write_sectors;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t queue_full_events;
    uint64_t media_errors;
    uint64_t retries;
    uint32_t p50_read_latency_us;
    uint32_t p99_read_latency_us;
    uint32_t p50_write_latency_us;
    uint32_t p99_write_latency_us;
} __attribute__((packed));

/* Fallback for io_uring_prep_cmd if not available */
#ifndef IORING_OP_URING_CMD
#define IORING_OP_URING_CMD 34
static inline void io_uring_prep_cmd(struct io_uring_sqe *sqe, int fd,
                                     int cmd, int arg1, int arg2, 
                                     void *addr, size_t len)
{
    io_uring_prep_rw(IORING_OP_URING_CMD, sqe, fd, addr, len, 0);
    sqe->len = len;
    sqe->buf_index = cmd;
    sqe->rw_flags = arg1;
    sqe->buf_group = arg2;
}
#endif

#define DEFAULT_DEVICE "/dev/uringblk0"
#define TEST_BLOCK_SIZE 4096
#define TEST_QUEUE_DEPTH 64
#define TEST_IO_COUNT 1000

struct test_config {
    const char *device;
    int queue_depth;
    int io_count;
    bool use_poll;
    bool use_fixed_buffers;
    bool test_admin;
    bool verbose;
};

static struct test_config config = {
    .device = DEFAULT_DEVICE,
    .queue_depth = TEST_QUEUE_DEPTH,
    .io_count = TEST_IO_COUNT,
    .use_poll = false,
    .use_fixed_buffers = false,
    .test_admin = false,
    .verbose = false,
};

/* Test buffer management */
static void *test_buffers = NULL;
static size_t buffer_size;

static int setup_test_buffers(void)
{
    buffer_size = TEST_BLOCK_SIZE * config.queue_depth;
    test_buffers = aligned_alloc(TEST_BLOCK_SIZE, buffer_size);
    if (!test_buffers) {
        perror("aligned_alloc");
        return -1;
    }
    
    /* Fill with test pattern */
    for (size_t i = 0; i < buffer_size; i++) {
        ((char*)test_buffers)[i] = i % 256;
    }
    
    return 0;
}

static void cleanup_test_buffers(void)
{
    if (test_buffers) {
        free(test_buffers);
        test_buffers = NULL;
    }
}

/* URING_CMD test functions */
static int test_uring_cmd_identify(int fd, struct io_uring *ring)
{
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    struct uringblk_ucmd_hdr hdr;
    struct uringblk_identify id;
    char cmd_buf[sizeof(hdr) + sizeof(id)];
    int ret;

    printf("Testing URING_CMD IDENTIFY...\n");

    /* Prepare command header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.abi_major = URINGBLK_ABI_MAJOR;
    hdr.abi_minor = URINGBLK_ABI_MINOR;
    hdr.opcode = URINGBLK_UCMD_IDENTIFY;
    hdr.payload_len = sizeof(id);

    /* Copy header and prepare buffer */
    memcpy(cmd_buf, &hdr, sizeof(hdr));
    memset(cmd_buf + sizeof(hdr), 0, sizeof(id));

    /* Submit URING_CMD */
    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "Failed to get SQE\n");
        return -1;
    }

    io_uring_prep_cmd(sqe, fd, 0, 0, 0, cmd_buf, sizeof(cmd_buf));
    
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit failed: %s\n", strerror(-ret));
        return ret;
    }

    /* Wait for completion */
    ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "io_uring_wait_cqe failed: %s\n", strerror(-ret));
        return ret;
    }

    if (cqe->res < 0) {
        fprintf(stderr, "URING_CMD IDENTIFY failed: %s\n", strerror(-cqe->res));
        io_uring_cqe_seen(ring, cqe);
        return cqe->res;
    }

    /* Parse response */
    memcpy(&id, cmd_buf + sizeof(hdr), sizeof(id));
    
    printf("Device Identity:\n");
    printf("  Model: %.40s\n", id.model);
    printf("  Firmware: %.16s\n", id.firmware);
    printf("  Logical block size: %u bytes\n", id.logical_block_size);
    printf("  Physical block size: %u bytes\n", id.physical_block_size);
    printf("  Capacity: %" PRIu64 " sectors\n", id.capacity_sectors);
    printf("  Features: 0x%" PRIx64 "\n", id.features_bitmap);
    printf("  Queue count: %u\n", id.queue_count);
    printf("  Queue depth: %u\n", id.queue_depth);
    printf("  Max segments: %u\n", id.max_segments);

    io_uring_cqe_seen(ring, cqe);
    return 0;
}

static int test_uring_cmd_get_stats(int fd, struct io_uring *ring)
{
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    struct uringblk_ucmd_hdr hdr;
    struct uringblk_stats stats;
    char cmd_buf[sizeof(hdr) + sizeof(stats)];
    int ret;

    printf("Testing URING_CMD GET_STATS...\n");

    /* Prepare command header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.abi_major = URINGBLK_ABI_MAJOR;
    hdr.abi_minor = URINGBLK_ABI_MINOR;
    hdr.opcode = URINGBLK_UCMD_GET_STATS;
    hdr.payload_len = sizeof(stats);

    /* Copy header and prepare buffer */
    memcpy(cmd_buf, &hdr, sizeof(hdr));
    memset(cmd_buf + sizeof(hdr), 0, sizeof(stats));

    /* Submit URING_CMD */
    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "Failed to get SQE\n");
        return -1;
    }

    io_uring_prep_cmd(sqe, fd, 0, 0, 0, cmd_buf, sizeof(cmd_buf));
    
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit failed: %s\n", strerror(-ret));
        return ret;
    }

    /* Wait for completion */
    ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "io_uring_wait_cqe failed: %s\n", strerror(-ret));
        return ret;
    }

    if (cqe->res < 0) {
        fprintf(stderr, "URING_CMD GET_STATS failed: %s\n", strerror(-cqe->res));
        io_uring_cqe_seen(ring, cqe);
        return cqe->res;
    }

    /* Parse response */
    memcpy(&stats, cmd_buf + sizeof(hdr), sizeof(stats));
    
    printf("Device Statistics:\n");
    printf("  Read ops: %" PRIu64 "\n", stats.read_ops);
    printf("  Write ops: %" PRIu64 "\n", stats.write_ops);
    printf("  Flush ops: %" PRIu64 "\n", stats.flush_ops);
    printf("  Discard ops: %" PRIu64 "\n", stats.discard_ops);
    printf("  Read bytes: %" PRIu64 "\n", stats.read_bytes);
    printf("  Write bytes: %" PRIu64 "\n", stats.write_bytes);
    printf("  Queue full events: %" PRIu64 "\n", stats.queue_full_events);
    printf("  Media errors: %" PRIu64 "\n", stats.media_errors);

    io_uring_cqe_seen(ring, cqe);
    return 0;
}

/* I/O test functions */
static int test_basic_io(int fd, struct io_uring *ring)
{
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    char *write_buf, *read_buf;
    int ret;

    printf("Testing basic I/O operations...\n");

    write_buf = aligned_alloc(TEST_BLOCK_SIZE, TEST_BLOCK_SIZE);
    read_buf = aligned_alloc(TEST_BLOCK_SIZE, TEST_BLOCK_SIZE);
    if (!write_buf || !read_buf) {
        perror("aligned_alloc");
        ret = -ENOMEM;
        goto cleanup;
    }

    /* Fill write buffer with pattern */
    memset(write_buf, 0x42, TEST_BLOCK_SIZE);
    memset(read_buf, 0, TEST_BLOCK_SIZE);

    /* Test write */
    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "Failed to get SQE for write\n");
        ret = -1;
        goto cleanup;
    }

    io_uring_prep_write(sqe, fd, write_buf, TEST_BLOCK_SIZE, 0);
    
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "Write submit failed: %s\n", strerror(-ret));
        goto cleanup;
    }

    ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "Write wait failed: %s\n", strerror(-ret));
        goto cleanup;
    }

    if (cqe->res != TEST_BLOCK_SIZE) {
        fprintf(stderr, "Write failed: expected %d, got %d\n", 
                TEST_BLOCK_SIZE, cqe->res);
        ret = cqe->res < 0 ? cqe->res : -EIO;
        io_uring_cqe_seen(ring, cqe);
        goto cleanup;
    }

    io_uring_cqe_seen(ring, cqe);
    printf("  Write test passed (%d bytes)\n", TEST_BLOCK_SIZE);

    /* Test read */
    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "Failed to get SQE for read\n");
        ret = -1;
        goto cleanup;
    }

    io_uring_prep_read(sqe, fd, read_buf, TEST_BLOCK_SIZE, 0);
    
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "Read submit failed: %s\n", strerror(-ret));
        goto cleanup;
    }

    ret = io_uring_wait_cqe(ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "Read wait failed: %s\n", strerror(-ret));
        goto cleanup;
    }

    if (cqe->res != TEST_BLOCK_SIZE) {
        fprintf(stderr, "Read failed: expected %d, got %d\n", 
                TEST_BLOCK_SIZE, cqe->res);
        ret = cqe->res < 0 ? cqe->res : -EIO;
        io_uring_cqe_seen(ring, cqe);
        goto cleanup;
    }

    io_uring_cqe_seen(ring, cqe);

    /* Verify data */
    if (memcmp(write_buf, read_buf, TEST_BLOCK_SIZE) != 0) {
        fprintf(stderr, "Data verification failed\n");
        ret = -EIO;
        goto cleanup;
    }

    printf("  Read test passed (%d bytes)\n", TEST_BLOCK_SIZE);
    printf("  Data verification passed\n");
    ret = 0;

cleanup:
    if (write_buf) free(write_buf);
    if (read_buf) free(read_buf);
    return ret;
}

static int test_performance(int fd, struct io_uring *ring)
{
    struct timespec start, end;
    double elapsed, iops, bandwidth;
    int submitted = 0, completed = 0;
    int ret;

    printf("Testing performance (%d operations)...\n", config.io_count);

    clock_gettime(CLOCK_MONOTONIC, &start);

    while (completed < config.io_count) {
        /* Submit operations */
        while (submitted < config.io_count && 
               submitted - completed < config.queue_depth) {
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            if (!sqe)
                break;

            size_t offset = (submitted % config.queue_depth) * TEST_BLOCK_SIZE;
            io_uring_prep_read(sqe, fd, 
                             (char*)test_buffers + offset,
                             TEST_BLOCK_SIZE, 
                             (submitted * TEST_BLOCK_SIZE) % (1024 * 1024));
            submitted++;
        }

        ret = io_uring_submit(ring);
        if (ret < 0) {
            fprintf(stderr, "Submit failed: %s\n", strerror(-ret));
            return ret;
        }

        /* Complete operations */
        struct io_uring_cqe *cqe;
        while (completed < submitted) {
            ret = io_uring_wait_cqe(ring, &cqe);
            if (ret < 0) {
                fprintf(stderr, "Wait failed: %s\n", strerror(-ret));
                return ret;
            }

            if (cqe->res < 0) {
                fprintf(stderr, "I/O failed: %s\n", strerror(-cqe->res));
                io_uring_cqe_seen(ring, cqe);
                return cqe->res;
            }

            completed++;
            io_uring_cqe_seen(ring, cqe);
            
            if (config.verbose && completed % 100 == 0) {
                printf("  Completed %d/%d operations\n", completed, config.io_count);
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) + 
              (end.tv_nsec - start.tv_nsec) / 1000000000.0;

    iops = config.io_count / elapsed;
    bandwidth = (iops * TEST_BLOCK_SIZE) / (1024 * 1024); /* MB/s */

    printf("Performance Results:\n");
    printf("  Operations: %d\n", config.io_count);
    printf("  Queue depth: %d\n", config.queue_depth);
    printf("  Elapsed time: %.3f seconds\n", elapsed);
    printf("  IOPS: %.0f\n", iops);
    printf("  Bandwidth: %.1f MB/s\n", bandwidth);

    return 0;
}

static void print_usage(const char *progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -d, --device DEVICE    Device path (default: %s)\n", DEFAULT_DEVICE);
    printf("  -q, --queue-depth N    Queue depth (default: %d)\n", TEST_QUEUE_DEPTH);
    printf("  -c, --count N          Number of I/O operations (default: %d)\n", TEST_IO_COUNT);
    printf("  -p, --poll             Use polling mode\n");
    printf("  -f, --fixed-buffers    Use fixed buffers\n");
    printf("  -a, --admin            Test admin commands\n");
    printf("  -v, --verbose          Verbose output\n");
    printf("  -h, --help             Show this help\n");
}

int main(int argc, char *argv[])
{
    struct io_uring ring;
    int fd = -1;
    int ret = 0;
    int flags = 0;

    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"queue-depth", required_argument, 0, 'q'},
        {"count", required_argument, 0, 'c'},
        {"poll", no_argument, 0, 'p'},
        {"fixed-buffers", no_argument, 0, 'f'},
        {"admin", no_argument, 0, 'a'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:q:c:pfavh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            config.device = optarg;
            break;
        case 'q':
            config.queue_depth = atoi(optarg);
            if (config.queue_depth <= 0) {
                fprintf(stderr, "Invalid queue depth: %s\n", optarg);
                exit(1);
            }
            break;
        case 'c':
            config.io_count = atoi(optarg);
            if (config.io_count <= 0) {
                fprintf(stderr, "Invalid I/O count: %s\n", optarg);
                exit(1);
            }
            break;
        case 'p':
            config.use_poll = true;
            break;
        case 'f':
            config.use_fixed_buffers = true;
            break;
        case 'a':
            config.test_admin = true;
            break;
        case 'v':
            config.verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            exit(0);
        default:
            print_usage(argv[0]);
            exit(1);
        }
    }

    printf("uringblk test program\n");
    printf("Device: %s\n", config.device);
    printf("Queue depth: %d\n", config.queue_depth);
    printf("Polling: %s\n", config.use_poll ? "enabled" : "disabled");
    printf("Fixed buffers: %s\n", config.use_fixed_buffers ? "enabled" : "disabled");
    printf("\n");

    /* Open device */
    fd = open(config.device, O_RDWR | O_DIRECT);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "Make sure the uringblk driver is loaded and %s exists\n", 
                config.device);
        exit(1);
    }

    /* Setup test buffers */
    if (setup_test_buffers() < 0) {
        ret = 1;
        goto cleanup;
    }

    /* Initialize io_uring */
    if (config.use_poll)
        flags |= IORING_SETUP_IOPOLL;

    ret = io_uring_queue_init(config.queue_depth, &ring, flags);
    if (ret < 0) {
        fprintf(stderr, "io_uring_queue_init failed: %s\n", strerror(-ret));
        ret = 1;
        goto cleanup;
    }

    /* Register fixed buffers if requested */
    if (config.use_fixed_buffers) {
        struct iovec iov = {
            .iov_base = test_buffers,
            .iov_len = buffer_size,
        };
        
        ret = io_uring_register_buffers(&ring, &iov, 1);
        if (ret < 0) {
            fprintf(stderr, "io_uring_register_buffers failed: %s\n", strerror(-ret));
            ret = 1;
            goto cleanup_ring;
        }
        printf("Fixed buffers registered\n");
    }

    /* Run tests */
    printf("=== Basic I/O Test ===\n");
    ret = test_basic_io(fd, &ring);
    if (ret) {
        fprintf(stderr, "Basic I/O test failed\n");
        goto cleanup_ring;
    }
    printf("\n");

    if (config.test_admin) {
        printf("=== Admin Command Tests ===\n");
        ret = test_uring_cmd_identify(fd, &ring);
        if (ret) {
            fprintf(stderr, "IDENTIFY test failed\n");
            goto cleanup_ring;
        }
        printf("\n");

        ret = test_uring_cmd_get_stats(fd, &ring);
        if (ret) {
            fprintf(stderr, "GET_STATS test failed\n");
            goto cleanup_ring;
        }
        printf("\n");
    }

    printf("=== Performance Test ===\n");
    ret = test_performance(fd, &ring);
    if (ret) {
        fprintf(stderr, "Performance test failed\n");
        goto cleanup_ring;
    }

    printf("\nAll tests completed successfully!\n");
    ret = 0;

cleanup_ring:
    io_uring_queue_exit(&ring);
cleanup:
    cleanup_test_buffers();
    if (fd >= 0) close(fd);
    return ret;
}