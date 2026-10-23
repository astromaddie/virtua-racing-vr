/* Test the actual topology selection and scoped restoration with an OS seam.
   New threads inherit the caller's mask, so external-call boundaries must see
   the original mask, including after failed placement and cleanup. */
#include <stdint.h>
#include <assert.h>
#include <string.h>
static uint64_t allowed, current, other_thread;
static unsigned capacities[64], get_calls, set_calls, capacity_reads, fail_set;
static int fail_get;
static int cpu_get(uint64_t *mask) { get_calls++; *mask=allowed; return !fail_get && allowed; }
static int cpu_set(uint64_t mask) { set_calls++; if(set_calls==fail_set)return 0; current=mask; return 1; }
static unsigned cpu_capacity(unsigned cpu) { capacity_reads++; return capacities[cpu]; }
#define VR_CPU_TEST
#include "../src/vr_cpu.c"

static void reset(uint64_t mask)
{
    memset(&placement,0,sizeof(placement));
    memset(capacities,0,sizeof(capacities));
    allowed=current=mask; other_thread=0x3f;
    get_calls=set_calls=capacity_reads=fail_set=fail_get=0;
}
static void frame_capacity(void)
{
    capacities[0]=capacities[1]=216;
    capacities[2]=capacities[3]=capacities[4]=855;
    capacities[5]=capacities[6]=803;
    capacities[7]=1024;
}
static void external_boundary(void)
{
    assert(current==allowed); /* A lazily created driver/audio/XR worker inherits this. */
    assert(other_thread==0x3f); /* Existing workers are never changed. */
}
int main(void)
{
    reset(0xf8); frame_capacity(); vr_cpu_init(1);
    assert(placement.enabled && placement.preferred==0x80 && get_calls==1 && capacity_reads==5);
    for(unsigned i=0;i<4;i++) {
        vr_cpu_enter(); assert(current==0x80);
        vr_cpu_enter(); /* Nested accidental entry cannot lose the original mask. */
        assert(vr_cpu_leave()); external_boundary();
    }
    assert(set_calls==8 && get_calls==1 && capacity_reads==5); /* No topology I/O per frame. */
    vr_cpu_enter(); vr_cpu_shutdown(); external_boundary();

    reset(0x78); frame_capacity(); vr_cpu_init(1);
    assert(placement.preferred==0x18); /* X4 excluded: both fastest permitted A720s. */
    vr_cpu_enter(); assert(current==0x18); assert(vr_cpu_leave()); external_boundary();

    reset(0x18); frame_capacity(); vr_cpu_init(1);
    vr_cpu_enter(); assert(vr_cpu_leave()); assert(set_calls==0); external_boundary();
    reset(0xf8); frame_capacity(); capacities[6]=0; vr_cpu_init(1);
    vr_cpu_enter(); assert(vr_cpu_leave()); assert(set_calls==0); external_boundary();
    reset(0xf8); frame_capacity(); vr_cpu_init(0);
    vr_cpu_enter(); assert(vr_cpu_leave()); assert(get_calls==0 && set_calls==0); external_boundary();
    reset(0xf8); fail_get=1; vr_cpu_init(1);
    vr_cpu_enter(); assert(vr_cpu_leave()); assert(set_calls==0); external_boundary();

    reset(0xf8); frame_capacity(); vr_cpu_init(1); fail_set=1;
    vr_cpu_enter(); assert(vr_cpu_leave()); external_boundary();
    vr_cpu_enter(); assert(set_calls==1); /* Failed narrowing disables repeated attempts. */

    reset(0xf8); frame_capacity(); vr_cpu_init(1); fail_set=2;
    vr_cpu_enter(); assert(!vr_cpu_leave()); /* Caller must stop before external work. */
    assert(!placement.enabled && placement.entered);
    vr_cpu_shutdown(); external_boundary(); /* Cleanup retries restoration. */

    reset(UINT64_C(1)<<63 | 1); capacities[0]=100; capacities[63]=200; vr_cpu_init(1);
    vr_cpu_enter(); assert(current==(UINT64_C(1)<<63)); assert(vr_cpu_leave()); external_boundary();
    puts("CPU scopes: allowed topology, ties, missing data, opt-out, failures and worker-safe restoration passed");
}
