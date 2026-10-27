// SPDX-License-Identifier: GPL-2.0-only
/* RAM-only first-boot diagnostic. It never mounts block devices. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void show(const char *path)
{
	char buf[4096];
	size_t count;
	FILE *file = fopen(path, "r");
	printf("\n--- %s ---\n", path);
	if (!file) { printf("unavailable: %s\n", strerror(errno)); return; }
	while ((count = fread(buf, 1, sizeof(buf), file)))
		fwrite(buf, 1, count, stdout);
	fclose(file);
}

static void status(void)
{
	show("/proc/cmdline");
	show("/sys/devices/system/cpu/online");
	show("/proc/cpuinfo");
	show("/proc/meminfo");
	show("/proc/interrupts");
	show("/proc/mounts");
}

static void memtest(void)
{
	const size_t words = 16 * 1024 * 1024 / sizeof(unsigned int);
	volatile unsigned int *data = malloc(words * sizeof(*data));
	struct timespec start, end;
	unsigned int pass;
	size_t index;
	if (!data) { puts("allocation failed"); return; }
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (pass = 0; pass < 3; pass++) {
		for (index = 0; index < words; index++)
			data[index] = (unsigned int)index ^ (0x5a5a1234u * (pass + 1));
		for (index = 0; index < words; index++)
			if (data[index] != ((unsigned int)index ^ (0x5a5a1234u * (pass + 1)))) {
				printf("MEMTEST FAIL pass=%u word=%zu\n", pass, index);
				free((void *)data); return;
			}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	printf("MEMTEST PASS: 16 MiB, 3 patterns, %.3f s; not a DMA/coherency certification\n",
	       end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9);
	free((void *)data);
}

int main(void)
{
	char line[80];
	setvbuf(stdout, NULL, _IONBF, 0);
	mkdir("/proc", 0555); mkdir("/sys", 0555); mkdir("/dev", 0755);
	if (mount("proc", "/proc", "proc", 0, NULL)) perror("mount proc");
	if (mount("sysfs", "/sys", "sysfs", 0, NULL)) perror("mount sysfs");
	if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL)) perror("mount devtmpfs");
	puts("\nHI3798 MV100 EXPERIMENTAL RAM DIAGNOSTIC - no block mounts\n");
	status();
	puts("Commands: status, memtest, help. Reset/power cycle to return to the saved boot path.");
	for (;;) {
		printf("mv100-probe> ");
		if (!fgets(line, sizeof(line), stdin)) { clearerr(stdin); sleep(1); continue; }
		if (!strncmp(line, "status", 6)) status();
		else if (!strncmp(line, "memtest", 7)) memtest();
		else puts("Commands: status, memtest, help. No disk-writing or environment commands.");
	}
}
