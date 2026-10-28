// SPDX-License-Identifier: GPL-2.0-only
/* RAM-only diagnostic. `fs` mounts block devices read-only with noload, so a
 * dirty ext4 journal is never replayed. `rootboot` is the only command that
 * mounts read-write, and only when explicitly invoked. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

static void listdir(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *entry;
	unsigned int count = 0;

	printf("\n--- %s/ ---\n", path);
	if (!dir) { printf("unavailable: %s\n", strerror(errno)); return; }
	while ((entry = readdir(dir)) && count < 32) {
		if (entry->d_name[0] == '.')
			continue;
		printf("%s  ", entry->d_name);
		count++;
	}
	if (!count)
		printf("(empty)");
	printf("\n");
	closedir(dir);
}

static void status(void)
{
	show("/proc/cmdline");
	show("/sys/devices/system/cpu/online");
	show("/proc/cpuinfo");
	show("/proc/meminfo");
	show("/proc/interrupts");
	show("/proc/partitions");
	listdir("/sys/block");
	listdir("/sys/class/mmc_host");
	show("/proc/mounts");
}

/* Read-only, noload: refuse to touch the journal; never writes the device. */
static void try_mount(const char *device)
{
	struct stat info;
	if (stat(device, &info)) {
		printf("%s: absent (%s)\n", device, strerror(errno));
		return;
	}
	if (mount(device, "/mnt", "ext4", MS_RDONLY, "noload")) {
		printf("%s: mount ro,noload failed: %s\n", device, strerror(errno));
		return;
	}
	printf("%s: mounted read-only (noload)\n", device);
	listdir("/mnt");
	show("/mnt/etc/os-release");
	show("/mnt/etc/hostname");
	if (umount("/mnt"))
		printf("%s: umount failed: %s\n", device, strerror(errno));
	else
		printf("%s: unmounted\n", device);
}

static void fsprobe(void)
{
	static const char *const candidates[] = {
		"/dev/mmcblk0p4",	/* eMMC rootfs */
		"/dev/sda4",		/* USB SSD rootfs used by the 4.4 kernel */
		"/dev/mmcblk1p1",	/* SD card */
	};
	unsigned int index;

	show("/proc/partitions");
	listdir("/sys/block");
	listdir("/sys/class/mmc_host");
	show("/proc/filesystems");
	mkdir("/mnt", 0755);
	/* USB storage sits behind hubs and finishes probing well after init. */
	sleep(3);
	puts("\n=== after 3 s settle ===");
	show("/proc/partitions");
	listdir("/sys/block");
	for (index = 0; index < sizeof(candidates) / sizeof(candidates[0]); index++)
		try_mount(candidates[index]);
	puts("\nRead-only probe only; no journal replay, no writes.");
}

static int have_dir(const char *path)
{
	struct stat info;
	return !stat(path, &info) && S_ISDIR(info.st_mode);
}

/* access()/stat() would resolve an absolute symlink against the *current* root,
 * not the new one, so resolve readlink() targets inside the target tree first. */
static int resolve_in_root(const char *root, const char *relative,
			   struct stat *info)
{
	char path[512];
	char link[512];
	char resolved[512];
	ssize_t length;
	int written;
	char *slash;

	written = snprintf(path, sizeof(path), "%s%s", root, relative);
	if (written < 0 || (size_t)written >= sizeof(path))
		return -1;
	length = readlink(path, link, sizeof(link) - 1);
	if (length > 0) {
		link[length] = '\0';
		printf("%s -> %s\n", path, link);
		if (link[0] == '/') {
			written = snprintf(resolved, sizeof(resolved), "%s%s",
					   root, link);
		} else {
			/* Relative target: resolve inside the target root, in the
			 * directory that holds the link. Kernel ".." handling keeps
			 * the walk under /newroot as long as it stays relative. */
			slash = strrchr(path, '/');
			if (!slash)
				return -1;
			*slash = '\0';
			written = snprintf(resolved, sizeof(resolved), "%s/%s",
					   path, link);
		}
		if (written < 0 || (size_t)written >= sizeof(resolved))
			return -1;
		if (snprintf(path, sizeof(path), "%s", resolved) < 0)
			return -1;
	}
	return stat(path, info);
}

/* Hand over to the real Debian root. Every precondition is checked before any
 * mount is moved, so a failure leaves the diagnostic shell usable. */
static void rootboot(const char *command)
{
	const char *device = "/dev/mmcblk0p4";
	const char *target = "/newroot";
	struct stat info;

	mkdir(target, 0755);
	if (mount(device, target, "ext4", 0, NULL)) {
		printf("mount %s on %s failed: %s\n", device, target, strerror(errno));
		return;
	}
	printf("%s mounted read-write on %s\n", device, target);
	if (resolve_in_root(target, command, &info)) {
		printf("cannot stat %s: %s\n", command, strerror(errno));
		umount(target);
		return;
	}
	if (!S_ISREG(info.st_mode) || !(info.st_mode & 0111)) {
		printf("%s has mode %o; not a regular executable\n",
		       command, info.st_mode & 07777);
		umount(target);
		return;
	}
	if (!have_dir("/newroot/proc") || !have_dir("/newroot/sys") ||
	    !have_dir("/newroot/dev")) {
		puts("target root lacks /proc, /sys or /dev; unmounting");
		umount(target);
		return;
	}
	printf("Handing over to %s; this shell will be lost.\n", command);
	if (mount("/proc", "/newroot/proc", NULL, MS_MOVE, NULL)) {
		perror("move /proc"); return;
	}
	if (mount("/sys", "/newroot/sys", NULL, MS_MOVE, NULL)) {
		perror("move /sys"); return;
	}
	if (mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL)) {
		perror("move /dev"); return;
	}
	if (chdir(target) || chroot(".") || chdir("/")) {
		perror("chroot"); return;
	}
	execl(command, command, NULL);
	perror("exec");
	for (;;)
		pause();
}

/* Raw futex syscalls, to separate "kernel futex broken" from "systemd uses it
 * unusually". Constants are local so the cross headers cannot drift. */
#define PROBE_FUTEX_WAIT     0
#define PROBE_FUTEX_WAKE     1
#define PROBE_FUTEX_PRIVATE  128

static void futex_call(const char *label, int op, int expected)
{
	int word = 0;
	struct timespec timeout = { 0, 2000000 };
	long result;
	int saved;

	errno = 0;
	result = syscall(SYS_futex, &word, op, expected, &timeout, NULL, 0);
	saved = errno;
	printf("%-22s ret=%ld errno=%d (%s)\n", label, result, saved,
	       strerror(saved));
}

static void futextest(void)
{
	printf("SYS_futex=%d\n", (int)SYS_futex);
	futex_call("WAKE shared", PROBE_FUTEX_WAKE, 1);
	futex_call("WAKE private", PROBE_FUTEX_WAKE | PROBE_FUTEX_PRIVATE, 1);
	/* val != *word, so this must return EAGAIN immediately. */
	futex_call("WAIT mismatch(expect EAGAIN)", PROBE_FUTEX_WAIT, 1);
	futex_call("WAIT private mismatch", PROBE_FUTEX_WAIT | PROBE_FUTEX_PRIVATE, 1);
#ifdef SYS_set_tls
	printf("SYS_set_tls=%d\n", (int)SYS_set_tls);
#else
	puts("SYS_set_tls undefined in headers");
#endif
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

/* Fork a detached child that dumps kernel state on a timer.
 *
 * Serial SysRq needs a genuine break condition on the line, and the in-box
 * usbser.sys CDC-ACM driver on this USB adapter accepts SETBREAK but does not
 * deliver it, so the board has to trigger itself. After the handover the child
 * still owns the initramfs root, where /newroot/proc has become procfs once
 * rootboot moved /proc into the target root; the fallback covers the case where
 * the handover never happened. SysRq state is global, so this also dumps the
 * stacks of processes that PID1 started after the exec. */
static void sysrqwatch(int delay)
{
	pid_t pid = fork();
	FILE *null;
	int trigger;

	if (pid < 0) { perror("fork"); return; }
	if (pid > 0) {
		printf("sysrq watchdog armed: dump in %d s\n", delay);
		return;
	}

	setsid();
	null = fopen("/dev/null", "w");
	if (null) {
		dup2(fileno(null), STDOUT_FILENO);
		dup2(fileno(null), STDERR_FILENO);
	}
	sleep(delay);

	/* "w" lists every task blocked in uninterruptible sleep, with its stack. */
	trigger = open("/newroot/proc/sysrq-trigger", O_WRONLY);
	if (trigger < 0)
		trigger = open("/proc/sysrq-trigger", O_WRONLY);
	if (trigger < 0)
		_exit(1);
	if (write(trigger, "w", 1) != 1)
		_exit(1);
	close(trigger);

	/* "l" backtraces every online CPU, which catches a spinning core that is
	 * running rather than blocked and therefore invisible to "w". */
	sleep(3);
	trigger = open("/newroot/proc/sysrq-trigger", O_WRONLY);
	if (trigger < 0)
		trigger = open("/proc/sysrq-trigger", O_WRONLY);
	if (trigger >= 0) {
		if (write(trigger, "l", 1) != 1)
			_exit(1);
		close(trigger);
	}
	_exit(0);
}


/* One explicit RAM-only CPU-frequency trial, armed with a watchdog first. */
static int cpu_write(const char *name, const char *value)
{
    char path[160]; int fd; size_t len = strlen(value);
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpufreq/policy0/%s", name);
    fd = open(path, O_WRONLY);
    if (fd < 0) { perror(path); return -1; }
    if (write(fd, value, len) != (ssize_t)len) { perror(path); close(fd); return -1; }
    close(fd); return 0;
}
static void cpu_trial(unsigned int khz)
{
    int fd, secs = 8; char value[32];
    if (khz != 400000 && khz != 600000 && khz != 800000 && khz != 1000000 && khz != 1200000 && khz != 1500000) {
        puts("Unsupported nominal frequency"); return;
    }
    fd = open("/dev/watchdog0", O_WRONLY);
    if (fd < 0) { perror("watchdog0; refusing trial"); return; }
    if (ioctl(fd, WDIOC_SETTIMEOUT, &secs) < 0) { perror("watchdog timeout"); write(fd,"V",1); close(fd); return; }
    puts("CPU trial watchdog armed; block filesystems remain unmounted.");
    if (cpu_write("scaling_governor", "userspace")) goto done;
    snprintf(value, sizeof(value), "%u", khz);
    if (cpu_write("scaling_max_freq", value) || cpu_write("scaling_setspeed", value)) goto restore;
    show("/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq");
    memtest();
restore:
    cpu_write("scaling_setspeed", "1000000");
    cpu_write("scaling_max_freq", "1000000");
done:
    write(fd,"V",1); close(fd);
}

int main(void)
{
	char line[80];
	setvbuf(stdout, NULL, _IONBF, 0);
	mkdir("/proc", 0555); mkdir("/sys", 0555); mkdir("/dev", 0755);
	if (mount("proc", "/proc", "proc", 0, NULL)) perror("mount proc");
	if (mount("sysfs", "/sys", "sysfs", 0, NULL)) perror("mount sysfs");
	if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL)) perror("mount devtmpfs");
	puts("\nHI3798 MV100 EXPERIMENTAL RAM DIAGNOSTIC - block devices read-only only\n");
	status();
	puts("Commands: status, fs, rootboot [path], memtest, help. Reset/power cycle to return to the saved boot path.");
	for (;;) {
		printf("mv100-probe> ");
		if (!fgets(line, sizeof(line), stdin)) { clearerr(stdin); sleep(1); continue; }
		if (!strncmp(line, "status", 6)) status();
		else if (!strncmp(line, "rootboot", 8)) {
			char *argument = line + 8;
			char *end;
			while (*argument == ' ')
				argument++;
			end = strchr(argument, '\n');
			if (end)
				*end = '\0';
			rootboot(*argument == '/' ? argument : "/sbin/init");
		}
		else if (!strncmp(line, "fs", 2)) fsprobe();
		else if (!strncmp(line, "memtest", 7)) memtest();
		else if (!strncmp(line, "cpu ", 4)) cpu_trial(strtoul(line+4, NULL, 10));
		else if (!strncmp(line, "futextest", 9)) futextest();
		else if (!strncmp(line, "sysrqwatch", 10))
			sysrqwatch(atoi(line + 10) > 0 ? atoi(line + 10) : 60);
		else
			puts("Commands: status, fs, rootboot [path], futextest, memtest, "
			     "sysrqwatch [seconds], help. rootboot defaults to /sbin/init.");
	}
}
