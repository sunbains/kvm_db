#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <liburing.h>

struct uringblk_uring_cmd {
    __u16 opcode;
    __u16 flags;
    __u32 len;
    __u64 addr;
} __attribute__((packed));

int main() {
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    int fd, ret;
    struct uringblk_uring_cmd ucmd;
    char buffer[256];

    /* Open character admin device */
    fd = open("/dev/uringblk0-admin", O_RDWR);
    if (fd < 0) {
        perror("open /dev/uringblk0-admin");
        return 1;
    }

    printf("Opened character admin device fd=%d\n", fd);

    /* Initialize io_uring */
    ret = io_uring_queue_init(1, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "io_uring_queue_init failed: %s\n", strerror(-ret));
        close(fd);
        return 1;
    }

    /* Prepare URING_CMD */
    ucmd.opcode = 1; // URINGBLK_UCMD_IDENTIFY 
    ucmd.flags = 0;
    ucmd.len = sizeof(buffer);
    ucmd.addr = (uint64_t)buffer;

    sqe = io_uring_get_sqe(&ring);
    /* Manually prepare URING_CMD - generic version since liburing 2.5 doesn't have io_uring_prep_cmd() */
    memset(sqe, 0, sizeof(*sqe));
    sqe->opcode = IORING_OP_URING_CMD;
    sqe->fd = fd;
    /* Command goes in sqe->cmd (16 bytes), not sqe->addr */
    memcpy(sqe->cmd, &ucmd, sizeof(ucmd));
    /* DO NOT use cmd_op or socket-specific fields - this is the key difference */
    
    /* Debug: Print SQE details to verify correct preparation */
    printf("SQE details:\n");
    printf("  sqe->opcode = %u (should be %u for IORING_OP_URING_CMD)\n", sqe->opcode, IORING_OP_URING_CMD);
    printf("  sqe->fd = %d\n", sqe->fd);
    printf("  ucmd in sqe->cmd: opcode=%u, flags=%u, len=%u, addr=0x%llx\n", 
           ucmd.opcode, ucmd.flags, ucmd.len, (unsigned long long)ucmd.addr);
    
    /* Verify FD points to correct device */
    char fd_path[256], fd_target[256];
    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
    ssize_t link_len = readlink(fd_path, fd_target, sizeof(fd_target) - 1);
    if (link_len > 0) {
        fd_target[link_len] = '\0';
        printf("FD %d points to: %s\n", fd, fd_target);
    }
    
    printf("Testing URING_CMD on character device...\n");
    
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit failed: %s\n", strerror(-ret));
        goto cleanup;
    }

    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        fprintf(stderr, "io_uring_wait_cqe failed: %s\n", strerror(-ret));
        goto cleanup;
    }

    printf("Character device URING_CMD result: %d (%s)\n", 
           cqe->res, cqe->res < 0 ? strerror(-cqe->res) : "success");

    io_uring_cqe_seen(&ring, cqe);

cleanup:
    io_uring_queue_exit(&ring);
    close(fd);
    return 0;
}