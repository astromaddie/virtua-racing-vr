#include "vr_skids.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static vr_skids a,b;
static const vr_skid_point ground[3]={{-2000,0,-2000},{2000,0,-2000},{0,0,4000}};
static void project(void *user,const vr_skid_point q[4],unsigned colour)
{ vr_skids_project(user,q,ground,colour); }
static vr_skid_sample sample(float x,float z,float yaw)
{
    vr_skid_sample s={{x,4,z},{{x-14,4,z-20},{x+14,4,z-20}},yaw,{4.8f,4.8f},1,0};
    return s;
}
static void run(vr_skids *s,int car,int first,int n,float dx,float dz)
{
    for(int i=0;i<n;i++) {
        vr_skid_sample v=sample(i*dx,i*dz,0);
        vr_skids_step(s,car,first+i*3,&v,project,s);
    }
}
int main(void)
{
    vr_skids_reset(&a);
    run(&a,0,3,30,0,12); assert(a.count==0); /* straight */
    run(&a,0,200,30,0,-12); assert(a.count==0); /* reverse */
    run(&a,0,400,30,.2f,.2f); assert(a.count==0); /* parked jitter */
    vr_skids_reset(&a);
    for(int i=0;i<100;i++) {
        float yaw=i*.05f;
        vr_skid_sample v=sample(200*cosf(yaw),200*sinf(yaw),yaw);
        vr_skids_step(&a,0,3+i*3,&v,project,&a);
    }
    assert(a.count==0); /* tangent-aligned cornering, not a slide */
    for(int car=0;car<VR_SKID_CARS;car++) {
        vr_skids_reset(&a);
        for(int episode=0;episode<8;episode++)run(&a,car,3+episode*120,30,10,12);
        assert(a.emitted[car]>0);
    }
    /* Original sound eligibility makes even aligned high-speed motion eligible.
       Exercise both hysteresis thresholds, including episodes selected to skip. */
    vr_skids_reset(&a);
    for(int car=0;car<VR_SKID_CARS;car++) {
        for(int i=0;i<8;i++) {
            vr_skid_sample v=sample(0,i*12,0);
            v.sound_slip=i==1?33:i>1&&i<6?24:0;
            vr_skids_step(&a,car,3+i*3,&v,project,&a);
            if(i==5) {
                unsigned before=a.total;
                v.sound_slip=16; v.centre.z+=12;v.tyre[0].z+=12;v.tyre[1].z+=12;
                vr_skids_step(&a,car,3+(i+1)*3,&v,project,&a);
                assert(a.total==before);
            }
        }
    }
    assert(a.total>0);
    /* A geometric episode must never open the sound latch at the hold level. */
    vr_skids_reset(&a);
    run(&a,1,3,8,10,12);
    assert(a.cars[1].sliding && !a.cars[1].sound_active);
    unsigned geometric_total=a.total;
    vr_skid_sample held=a.cars[1].previous;
    held.centre.z+=12;held.tyre[0].z+=12;held.tyre[1].z+=12;held.sound_slip=24;
    vr_skids_step(&a,1,27,&held,project,&a);
    assert(!a.cars[1].sound_active && a.total==geometric_total);
    /* A true 33 entry survives zero travel / a snapshot gap, then holds at24. */
    held.sound_slip=33;
    vr_skids_step(&a,1,30,&held,project,&a);
    assert(a.cars[1].sound_active);
    held.sound_slip=24;
    vr_skids_step(&a,1,90,&held,project,&a);
    assert(a.cars[1].sound_active && a.total==geometric_total);
    held.centre.z+=12;held.tyre[0].z+=12;held.tyre[1].z+=12;
    vr_skids_step(&a,1,93,&held,project,&a);
    assert(a.cars[1].sound_active && a.cars[1].sliding);
    held.sound_slip=16;
    vr_skids_step(&a,1,96,&held,project,&a);
    assert(!a.cars[1].sound_active);
    vr_skids_reset(&a);vr_skids_reset(&b);
    for(int episode=0;episode<4;episode++){run(&a,0,3+episode*120,30,10,12);run(&b,0,3+episode*120,30,10,12);}
    assert(a.count>0 && !memcmp(&a,&b,sizeof a));
    unsigned count=a.count,total=a.total;
    vr_skid_sample v=a.cars[0].previous;
    vr_skids_step(&a,0,a.cars[0].frame,&v,project,&a);
    assert(a.count==count && a.total==total); /* repeated eye/display */
    v.centre.x+=1000;v.tyre[0].x+=1000;v.tyre[1].x+=1000;
    vr_skids_step(&a,0,453,&v,project,&a);assert(a.total==total); /* teleport */
    vr_skids_step(&a,0,800,&v,project,&a);assert(a.total==total); /* gap */
    v.valid=0;vr_skids_step(&a,0,803,&v,project,&a);assert(a.total==total);
    vr_skids_cut(&a);assert(a.count==count); /* camera/lifecycle continuity break */
    v=sample(0,0,0);vr_skids_step(&a,0,806,&v,project,&a);assert(a.total==total);
    vr_skids_reset(&a);assert(!a.count&&!a.total&&!a.cars[0].previous.valid);
    /* A ribbon partly outside a sloping road is clipped, with exact plane y. */
    const vr_skid_point slope[3]={{0,0,0},{100,10,0},{0,0,100}};
    const vr_skid_point q[4]={{-5,0,10},{5,.5f,10},{5,.5f,30},{-5,0,30}};
    vr_skids_project(&a,q,slope,0x600);assert(a.count>0);
    for(unsigned i=0;i<a.count;i++)for(int j=0;j<3;j++){
        vr_skid_point p=a.triangles[i].p[j];
        assert(p.x>=0 && p.z>=0 && p.x+p.z<=100);
        assert(fabsf(p.y-(p.x*.1f+.08f))<.0001f);
    }
    total=a.total;
    vr_skid_point airborne[4];memcpy(airborne,q,sizeof q);
    for(int i=0;i<4;i++)airborne[i].y+=20;
    vr_skids_project(&a,airborne,slope,0x600);assert(a.total==total);
    const vr_skid_point wall[3]={{0,0,0},{0,100,0},{0,0,100}};
    vr_skids_project(&a,q,wall,0x600);assert(a.total==total);
    const vr_skid_point outside[4]={{-20,0,0},{-10,0,0},{-10,0,10},{-20,0,10}};
    vr_skids_project(&a,outside,slope,0x600);assert(a.total==total);
    for(int i=0;i<VR_SKID_CAPACITY;i++) {
        a.frame=i;
        vr_skids_project(&a,q,slope,0x600);
    }
    assert(a.count==VR_SKID_CAPACITY && a.next<VR_SKID_CAPACITY);
    assert(a.total>VR_SKID_CAPACITY);
    for(unsigned i=1;i<a.count;i++)
        assert(vr_skids_triangle(&a,i-1)->frame<=vr_skids_triangle(&a,i)->frame);
    assert(vr_skids_triangle(&a,a.count-1)->frame==VR_SKID_CAPACITY-1);
    puts("skids: straight/reverse/jitter, deterministic slides, duplicate ticks, cuts, resets, slope/contact clipping and bounded ring passed");
}
