#include "vr_skids.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

void vr_skids_reset(vr_skids *s) { memset(s, 0, sizeof(*s)); }
void vr_skids_cut(vr_skids *s) { memset(s->cars, 0, sizeof(s->cars)); }
static float cross(vr_skid_point a, vr_skid_point b, vr_skid_point p)
{ return (b.x-a.x)*(p.z-a.z)-(b.z-a.z)*(p.x-a.x); }
int vr_road_overlay(const vr_skid_point triangle[3], const vr_skid_point road[3])
{
    float area=cross(road[0],road[1],road[2]);
    if(fabsf(area)<.01f)return 0;
    float ux=road[1].x-road[0].x,uy=road[1].y-road[0].y,uz=road[1].z-road[0].z;
    float vx=road[2].x-road[0].x,vy=road[2].y-road[0].y,vz=road[2].z-road[0].z;
    float nx=uy*vz-uz*vy,nz=ux*vy-uy*vx;
    if(area*area<.64f*(nx*nx+area*area+nz*nz))return 0;
    for(int i=0;i<3;i++) {
        float y=road[0].y+(nx*(triangle[i].x-road[0].x)+nz*(triangle[i].z-road[0].z))/area;
        if(fabsf(triangle[i].y-y)>.01f)return 0;
    }
    vr_skid_point a[8],b[8];int n=3;memcpy(a,triangle,3*sizeof(*a));
    for(int edge=0;edge<3&&n;edge++) {
        int m=0;
        for(int i=0;i<n;i++) {
            vr_skid_point p=a[i],q=a[(i+1)%n];
            float dp=cross(road[edge],road[(edge+1)%3],p)/area;
            float dq=cross(road[edge],road[(edge+1)%3],q)/area;
            if(dp>=0)b[m++]=p;
            if((dp<0)!=(dq<0)) {
                float t=dp/(dp-dq);
                b[m++]=(vr_skid_point){p.x+(q.x-p.x)*t,0,p.z+(q.z-p.z)*t};
            }
        }
        n=m;memcpy(a,b,n*sizeof(*a));
    }
    for(int i=1;i<n-1;i++)if(fabsf(cross(a[0],a[i],a[i+1]))>.01f)return 1;
    return 0;
}
static unsigned episode_hash(unsigned x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
static float angle_delta(float a, float b)
{ return remainderf(a-b, 6.28318530718f); }

void vr_skids_step(vr_skids *s, int car, int frame, const vr_skid_sample *v,
                   vr_skid_project project, void *user)
{
    if (car < 0 || car >= VR_SKID_CARS) return;
    vr_skid_car *c = &s->cars[car];
    if (frame == c->frame && c->previous.valid) return;
    /* The original sound latch is separate from cosmetic episode hysteresis.
       Keep it through slow/stationary samples and snapshot gaps: those interrupt
       ribbons, not the game's sound state. An absent car ends its latch. */
    if (!v->valid || abs(v->sound_slip)<=16) c->sound_active=0;
    else if (abs(v->sound_slip)>32) c->sound_active=1;
    const vr_skid_sample old = c->previous;
    int dt = frame-c->frame;
    c->previous = *v; c->frame = frame; s->frame=frame;
    float dx=v->centre.x-old.centre.x, dz=v->centre.z-old.centre.z;
    float distance=hypotf(dx,dz), turn=angle_delta(v->yaw,old.yaw);
    if (!v->valid || !old.valid || dt<1 || dt>8 || distance>200 || fabsf(turn)>.9f ||
        fabsf(v->centre.y-old.centre.y)>40) {
        c->warm=c->sliding=0;
        return;
    }
    /* Mid-tick heading prevents normal turning/chord motion registering as slip.
       Body forward is (-sin(yaw), cos(yaw)), exactly HLE rotate_y. */
    float yaw=old.yaw+turn*.5f;
    float slip=distance>.01f ? fabsf(dx*cosf(yaw)+dz*sinf(yaw))/distance : 0;
    float speed=distance*60/dt;
    /* Match the original sustained tyre sound controller (ROM 8885C4/888610).
       This also qualifies fast cornering with little body/velocity misalignment.
       Sampling the source state avoids the transient sound mailbox and does not
       depend on whether that particular opponent is selected for audible mixing. */
    int sound_slip=c->sound_active;
    if (distance<2 || (!sound_slip && (speed<120 || slip<(c->sliding ? .10f : .18f)))) {
        c->warm=c->sliding=0;
        return;
    }
    if (!c->sliding) {
        if (++c->warm<2 && !sound_slip) return;
        c->sliding=1;
        unsigned h=episode_hash((unsigned)car*0x9e3779b9u + ++c->episode);
        c->selected=(h&3)!=0; /* whole episodes, not flickering per-tick noise */
    }
    if (!c->selected || distance<2) return;
    unsigned shade=slip>.5f ? 5 : slip>.3f ? 6 : 7;
    shade += episode_hash(c->episode+(unsigned)car*37u)&1u;
    for (int tyre=0;tyre<2;tyre++) {
        float width=fminf(v->width[tyre],old.width[tyre])*.5f;
        if (width<.5f || width>6) continue;
        vr_skid_point q[4]={old.tyre[tyre],old.tyre[tyre],v->tyre[tyre],v->tyre[tyre]};
        q[0].x-=cosf(old.yaw)*width; q[0].z-=sinf(old.yaw)*width;
        q[1].x+=cosf(old.yaw)*width; q[1].z+=sinf(old.yaw)*width;
        q[2].x+=cosf(v->yaw)*width; q[2].z+=sinf(v->yaw)*width;
        q[3].x-=cosf(v->yaw)*width; q[3].z-=sinf(v->yaw)*width;
        unsigned before=s->total;
        project(user,q,shade<<8);
        s->emitted[car]+=s->total-before;
    }
}

void vr_skids_project(vr_skids *s, const vr_skid_point quad[4],
                      const vr_skid_point road[3], unsigned colour)
{
    float area=cross(road[0],road[1],road[2]);
    if (fabsf(area)<.01f) return;
    float ux=road[1].x-road[0].x, uy=road[1].y-road[0].y, uz=road[1].z-road[0].z;
    float vx=road[2].x-road[0].x, vy=road[2].y-road[0].y, vz=road[2].z-road[0].z;
    float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
    if (ny*ny < .8f*.8f*(nx*nx+ny*ny+nz*nz)) return;
    vr_skid_point a[10], b[10]; int n=4;
    memcpy(a,quad,4*sizeof(*a));
    for (int edge=0;edge<3 && n;edge++) {
        int m=0;
        for (int i=0;i<n;i++) {
            vr_skid_point p=a[i], q=a[(i+1)%n];
            float dp=cross(road[edge],road[(edge+1)%3],p)/area;
            float dq=cross(road[edge],road[(edge+1)%3],q)/area;
            if (dp>=0) b[m++]=p;
            if ((dp<0)!=(dq<0)) {
                float t=dp/(dp-dq);
                b[m++]=(vr_skid_point){p.x+(q.x-p.x)*t,p.y+(q.y-p.y)*t,p.z+(q.z-p.z)*t};
            }
        }
        n=m;memcpy(a,b,(size_t)n*sizeof(*a));
    }
    for (int i=0;i<n;i++) {
        float y=road[0].y-(nx*(a[i].x-road[0].x)+nz*(a[i].z-road[0].z))/ny;
        /* The original car ride height is ~4.2 units above the rendered asphalt;
           a small tolerance accepts that, but never projects airborne cars down. */
        if (fabsf(a[i].y-y)>6) return;
        a[i].y=y; /* The shared road-overlay draw supplies depth bias. */
    }
    for (int i=1;i<n-1;i++) {
        if (fabsf(cross(a[0],a[i],a[i+1]))<.01f) continue;
        s->triangles[s->next]=(vr_skid_triangle){{a[0],a[i],a[i+1]},colour,s->frame};
        s->next=(s->next+1)%VR_SKID_CAPACITY;
        if (s->count<VR_SKID_CAPACITY) s->count++;
        s->total++;
    }
}
