#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#include "../include/uapi/kdb.h"

#define TEST_SIZE_MB 256
#define TEST_SIZE (TEST_SIZE_MB * 1024 * 1024UL)
#define PAGE_SIZE 4096
#define LP_SIZE (1024 * 1024)  /* 1MB logical pages */

static void print_stats(int fd)
{
	struct kdb_stats stats;
	int ret;
	
	ret = ioctl(fd, KDB_GET_STATS, &stats);
	if (ret < 0) {
		perror("ioctl(KDB_GET_STATS)");
		return;
	}
	
	printf("=== KDB Statistics ===\n");
	printf("Total faults:      %llu\n", stats.total_faults);
	printf("Total mkwrite:     %llu\n", stats.total_mkwrite);
	printf("Total CP alloc:    %llu\n", stats.total_cp_alloc);
	printf("Total LP created:  %llu\n", stats.total_lp_created);
	printf("Dirty pages:       %llu\n", stats.dirty_pages);
	printf("Allocated CP:      %llu\n", stats.allocated_cp);
	printf("Allocated LP:      %llu\n", stats.allocated_lp);
	printf("=======================\n\n");
}

static int test_basic_mmap(int fd, void **mapped_mem)
{
	printf("=== Basic mmap test ===\n");
	
	/* Memory map the region */
	*mapped_mem = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE, 
			   MAP_SHARED, fd, 0);
	if (*mapped_mem == MAP_FAILED) {
		perror("mmap failed");
		return -1;
	}
	
	printf("Successfully mapped %d MB at %p\n", TEST_SIZE_MB, *mapped_mem);
	return 0;
}

static int test_zero_fill(void *mapped_mem)
{
	printf("=== Zero-fill test ===\n");
	
	volatile uint32_t *ptr = (volatile uint32_t *)mapped_mem;
	int errors = 0;
	
	/* Test first page */
	printf("Testing first page...\n");
	for (int i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) {
		if (ptr[i] != 0) {
			printf("ERROR: Non-zero value at offset %d: 0x%x\n", 
			       i * sizeof(uint32_t), ptr[i]);
			errors++;
			if (errors > 10) break;  /* Limit error output */
		}
	}
	
	/* Test a page in the middle */
	printf("Testing middle page...\n");
	ptr = (volatile uint32_t *)((char *)mapped_mem + TEST_SIZE / 2);
	for (int i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) {
		if (ptr[i] != 0) {
			printf("ERROR: Non-zero value at middle+%d: 0x%x\n", 
			       i * sizeof(uint32_t), ptr[i]);
			errors++;
			if (errors > 10) break;
		}
	}
	
	/* Test last page */
	printf("Testing last page...\n");
	ptr = (volatile uint32_t *)((char *)mapped_mem + TEST_SIZE - PAGE_SIZE);
	for (int i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) {
		if (ptr[i] != 0) {
			printf("ERROR: Non-zero value at end+%d: 0x%x\n", 
			       i * sizeof(uint32_t), ptr[i]);
			errors++;
			if (errors > 10) break;
		}
	}
	
	if (errors == 0) {
		printf("Zero-fill test PASSED\n");
	} else {
		printf("Zero-fill test FAILED with %d errors\n", errors);
	}
	
	return errors ? -1 : 0;
}

static int test_write_pattern(void *mapped_mem)
{
	printf("=== Write pattern test ===\n");
	
	volatile uint32_t *ptr = (volatile uint32_t *)mapped_mem;
	const uint32_t pattern = 0xDEADBEEF;
	int errors = 0;
	
	/* Write pattern to first few pages */
	printf("Writing pattern to first 16 pages...\n");
	for (int page = 0; page < 16; page++) {
		volatile uint32_t *page_ptr = ptr + (page * PAGE_SIZE / sizeof(uint32_t));
		for (int i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) {
			page_ptr[i] = pattern + page + i;
		}
	}
	
	/* Verify the pattern */
	printf("Verifying written pattern...\n");
	for (int page = 0; page < 16; page++) {
		volatile uint32_t *page_ptr = ptr + (page * PAGE_SIZE / sizeof(uint32_t));
		for (int i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) {
			uint32_t expected = pattern + page + i;
			if (page_ptr[i] != expected) {
				printf("ERROR: Page %d, offset %d: got 0x%x, expected 0x%x\n",
				       page, i * sizeof(uint32_t), page_ptr[i], expected);
				errors++;
				if (errors > 10) goto done;
			}
		}
	}
	
done:
	if (errors == 0) {
		printf("Write pattern test PASSED\n");
	} else {
		printf("Write pattern test FAILED with %d errors\n", errors);
	}
	
	return errors ? -1 : 0;
}

static int test_sparse_access(void *mapped_mem)
{
	printf("=== Sparse access test ===\n");
	
	const size_t stride = 1024 * 1024;  /* 1MB stride */
	volatile uint32_t *ptr = (volatile uint32_t *)mapped_mem;
	int errors = 0;
	
	/* Touch pages at 1MB intervals */
	printf("Touching pages at 1MB intervals...\n");
	for (size_t offset = 0; offset < TEST_SIZE; offset += stride) {
		volatile uint32_t *page_ptr = (volatile uint32_t *)((char *)mapped_mem + offset);
		uint32_t value = (uint32_t)(offset / stride);
		
		/* Write and read back */
		*page_ptr = value;
		if (*page_ptr != value) {
			printf("ERROR: At offset %zu: got 0x%x, expected 0x%x\n",
			       offset, *page_ptr, value);
			errors++;
		}
	}
	
	if (errors == 0) {
		printf("Sparse access test PASSED\n");
	} else {
		printf("Sparse access test FAILED with %d errors\n", errors);
	}
	
	return errors ? -1 : 0;
}

int main(void)
{
	int fd, ret = 0;
	void *mapped_mem = NULL;
	struct kdb_layout layout;
	
	printf("KDB mmap probe test starting...\n\n");
	
	/* Open the device */
	fd = open("/dev/kdbcache", O_RDWR);
	if (fd < 0) {
		perror("Failed to open /dev/kdbcache");
		printf("Make sure the kdb kernel module is loaded\n");
		return 1;
	}
	
	printf("Successfully opened /dev/kdbcache\n");
	
	/* Configure layout */
	layout.cp_size = PAGE_SIZE;
	layout.lp_size = LP_SIZE;
	layout.n_lpn = TEST_SIZE / LP_SIZE;
	
	printf("Configuring layout: cp_size=%llu, lp_size=%llu, n_lpn=%llu\n",
	       layout.cp_size, layout.lp_size, layout.n_lpn);
	
	ret = ioctl(fd, KDB_SET_LAYOUT, &layout);
	if (ret < 0) {
		perror("ioctl(KDB_SET_LAYOUT) failed");
		goto cleanup;
	}
	
	printf("Layout configured successfully\n\n");
	
	/* Print initial stats */
	print_stats(fd);
	
	/* Run tests */
	if (test_basic_mmap(fd, &mapped_mem) < 0) {
		ret = 1;
		goto cleanup;
	}
	
	print_stats(fd);
	
	if (test_zero_fill(mapped_mem) < 0) {
		ret = 1;
		goto cleanup;
	}
	
	print_stats(fd);
	
	if (test_write_pattern(mapped_mem) < 0) {
		ret = 1;
		goto cleanup;
	}
	
	print_stats(fd);
	
	if (test_sparse_access(mapped_mem) < 0) {
		ret = 1;
		goto cleanup;
	}
	
	print_stats(fd);
	
	printf("All tests completed!\n");
	
cleanup:
	if (mapped_mem && mapped_mem != MAP_FAILED) {
		munmap(mapped_mem, TEST_SIZE);
	}
	
	if (fd >= 0) {
		close(fd);
	}
	
	return ret;
}