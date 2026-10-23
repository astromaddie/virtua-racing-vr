#define _GNU_SOURCE
#include "vr_cpu.h"
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

/* Frame has eight CPUs. Machines with allowed CPUs above 63 or incomplete
   capacity information keep normal scheduling instead of guessing topology. */
#ifndef VR_CPU_TEST
#ifdef __linux__
#include <sched.h>
static int cpu_get(uint64_t *mask)
{
    cpu_set_t set;
    if (sched_getaffinity(0,sizeof(set),&set)) return 0;
    *mask=0;
    for(unsigned i=0;i<CPU_SETSIZE;i++) if(CPU_ISSET(i,&set)) {
        if(i>=64) return 0;
        *mask|=UINT64_C(1)<<i;
    }
    return *mask!=0;
}
static int cpu_set(uint64_t mask)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    for(unsigned i=0;i<64;i++) if(mask&(UINT64_C(1)<<i)) CPU_SET(i,&set);
    return sched_setaffinity(0,sizeof(set),&set)==0;
}
static unsigned cpu_capacity(unsigned cpu)
{
    char path[96];
    snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu%u/cpu_capacity",cpu);
    FILE *f=fopen(path,"r");
    unsigned value=0;
    if(f) { if(fscanf(f,"%u",&value)!=1)value=0; fclose(f); }
    return value;
}
#else
static int cpu_get(uint64_t *mask) { (void)mask; return 0; }
static int cpu_set(uint64_t mask) { (void)mask; return 0; }
static unsigned cpu_capacity(unsigned cpu) { (void)cpu; return 0; }
#endif
#endif

static struct { uint64_t original, preferred; int enabled, entered; } placement;

void vr_cpu_init(int enabled)
{
    placement.enabled=placement.entered=0;
    placement.original=placement.preferred=0;
    if(!enabled || !cpu_get(&placement.original)) return;
    unsigned highest=0;
    for(unsigned i=0;i<64;i++) if(placement.original&(UINT64_C(1)<<i)) {
        unsigned capacity=cpu_capacity(i);
        if(!capacity) return;
        if(capacity>highest) { highest=capacity; placement.preferred=0; }
        if(capacity==highest) placement.preferred|=UINT64_C(1)<<i;
    }
    placement.enabled=placement.preferred!=placement.original;
    printf("[vr] CPU scopes=%d allowed=0x%" PRIx64 " preferred=0x%" PRIx64 " capacity=%u\n",
           placement.enabled,placement.original,placement.preferred,highest);
}
void vr_cpu_enter(void)
{
    if(!placement.enabled || placement.entered) return;
    if(cpu_set(placement.preferred)) placement.entered=1;
    else {
        placement.enabled=0;
        fprintf(stderr,"[vr] CPU placement unavailable; keeping normal scheduling\n");
    }
}
int vr_cpu_leave(void)
{
    if(!placement.entered) return 1;
    if(cpu_set(placement.original)) { placement.entered=0; return 1; }
    placement.enabled=0;
    fprintf(stderr,"[vr] CPU mask restoration failed; stopping frame\n");
    return 0;
}
void vr_cpu_shutdown(void)
{
    vr_cpu_leave();
    placement.enabled=0;
}
