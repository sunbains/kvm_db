#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main() {
    int fd = open("/dev/uringblk0-admin", O_RDWR);
    if (fd < 0) {
        printf("Failed to open device: %s\n", strerror(errno));
        return 1;
    }
    
    printf("Successfully opened device fd=%d\n", fd);
    
    // Try a simple ioctl to verify the device works
    // Note: this should return an error but not crash
    int result = ioctl(fd, 0, 0);
    printf("ioctl result: %d (errno=%d: %s)\n", result, errno, strerror(errno));
    
    close(fd);
    return 0;
}
