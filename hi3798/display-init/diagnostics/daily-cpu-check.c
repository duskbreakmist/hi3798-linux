// SPDX-License-Identifier: MIT
/* One bounded arithmetic comparison at 1 GHz and 1.2 GHz, no disk I/O load. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>

static void setting(const char *name, const char *value)
{
    char path[160];
    snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpufreq/policy0/%s",name);
    FILE *f=fopen(path,"w");
    if(!f || fprintf(f,"%s\n",value)<0 || fclose(f)) { perror(path);exit(1); }
}
static double check(const char *khz)
{
    struct timespec a,b;
    uint32_t x=0x12345678;
    setting("scaling_setspeed",khz);
    usleep(10000);
    clock_gettime(CLOCK_MONOTONIC,&a);
    for(unsigned i=0;i<24000000;i++) { x^=x<<13; x^=x>>17; x^=x<<5; }
    clock_gettime(CLOCK_MONOTONIC,&b);
    double seconds=(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)*1e-9;
    printf("cpu_khz=%s seconds=%.6f checksum=%08x\n",khz,seconds,x);
    return seconds;
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    cpu_set_t mask;
    CPU_ZERO(&mask); CPU_SET(3,&mask);
    if (sched_setaffinity(0,sizeof(mask),&mask)) { perror("sched_setaffinity"); return 1; }
    setting("scaling_governor","userspace");
    setting("scaling_max_freq","1200000");
    double a=check("1000000"),b=check("1200000");
    /* Keep userspace at the tested daily rate; the boot service selects policy. */
    printf("nominal_1200_vs_1000_speedup=%.3f\n",a/b);
    return 0;
}
