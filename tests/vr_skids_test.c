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
    const vr_skid_point support_slope[3]={{0,0,0},{100,10,0},{0,0,100}};
    vr_skid_point paint[3]={{-10,-1,20},{30,3,20},{30,3,30}};
    assert(vr_road_overlay(paint,support_slope)); /* edge paint straddles asphalt */
    for(int i=0;i<3;i++)paint[i].y+=.1f;
    assert(!vr_road_overlay(paint,support_slope)); /* raised surface */
    const vr_skid_point neighbour[3]={{0,0,0},{-100,-10,0},{0,0,100}};
    assert(!vr_road_overlay(neighbour,support_slope)); /* shared edge is not overlap */
    const vr_skid_point support_wall[3]={{0,0,0},{0,100,0},{0,0,100}};
    assert(!vr_road_overlay(paint,support_wall));
    /* Real receiving planes, including a bank and an overhead deck. */
    vr_skid_point tyre[4]={{10,1,10},{20,1,10},{10,7,10},{20,7,10}};
    vr_skid_point centre={15,4,10};
    assert(fabsf(vr_road_clearance(tyre,4,centre,support_slope)+1)<1e-6);
    vr_skid_point deck[3];memcpy(deck,support_slope,sizeof deck);
    for(int i=0;i<3;i++)deck[i].y+=5;
    assert(isinf(vr_road_clearance(tyre,4,centre,deck)));
    for(int i=0;i<4;i++)tyre[i].y+=100;
    centre.y+=100;
    assert(fabsf(vr_road_clearance(tyre,4,centre,support_slope)-99)<1e-6);
    assert(isinf(vr_road_clearance(tyre,4,centre,support_wall)));
    const vr_skid_point steep[3]={{0,0,0},{100,200,0},{0,0,100}};
    const vr_skid_point steep_tyre[2]={{9,17,10},{11,21,10}};
    assert(fabsf(vr_road_clearance(steep_tyre,2,(vr_skid_point){10,24,10},steep)+1)<1e-6);
    const vr_skid_point slanted_wall[3]={{0,0,0},{2,200,0},{0,0,20}};
    const vr_skid_point wall_tyre[2]={{1,0,2},{1,8,2}};
    assert(isinf(vr_road_clearance(wall_tyre,2,(vr_skid_point){0,4,2},slanted_wall)));
    const vr_skid_point bank[3]={{-100,-50,-100},{100,50,-100},{0,0,100}};
    const vr_skid_point left[1]={{-10,-7,0}},right[1]={{10,3,0}};
    assert(vr_road_clearance(left,1,(vr_skid_point){-10,-1,0},bank)==-2);
    assert(vr_road_clearance(right,1,(vr_skid_point){10,9,0},bank)==-2);
    /* Footprint partition across a real slope discontinuity, including the
       authored part outside the receiver. Each projected piece lies on its own
       plane; the projection never changes the XZ outline. */
    vr_surface_polygon paint_piece={{{-2,-.5,1},{2,-.5,1},{0,-.5,4}},3},pieces[4];
    const vr_skid_point facet[3]={{0,0,0},{10,1,0},{0,0,10}};
    int pieces_count=vr_surface_project(&paint_piece,facet,pieces);assert(pieces_count>=2);
    double area_sum=0;
    for(int i=0;i<pieces_count;i++) {
        for(int j=0;j<pieces[i].count;j++) {
            vr_skid_point p=pieces[i].p[j];
            assert(p.x>=-2&&p.x<=2&&p.z>=1&&p.z<=4);
            assert(fabsf(p.y-(i?-.5f:p.x*.1f))<1e-5);
        }
        for(int j=1;j<pieces[i].count-1;j++) {
            vr_skid_point a=pieces[i].p[0],b=pieces[i].p[j],c=pieces[i].p[j+1];
            area_sum+=fabs((b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x));
        }
    }
    assert(fabs(area_sum-12)<1e-6);
    for(int i=0;i<3;i++)paint_piece.p[i].y+=10;
    assert(!vr_surface_project(&paint_piece,facet,pieces));
    assert(!vr_surface_project(&paint_piece,support_wall,pieces));
    const vr_skid_point door[4]={{0,0,10},{64,0,10},{64,32,10},{0,32,10}};
    vr_skid_point detail[4]={{8,4,10},{40,4,10},{40,15.5,10},{8,15.5,10}};
    assert(vr_surface_detail(detail,4,door,4));
    assert(!vr_surface_detail(door,4,detail,4));
    assert(!vr_surface_detail(door,4,door,4));
    detail[0].z+=.5f;assert(vr_surface_detail(detail,4,door,4));
    vr_skid_point on=vr_surface_on_plane(detail[0],door);assert(fabsf(on.z-10)<1e-6&&on.x==detail[0].x&&on.y==detail[0].y);
    detail[0].z+=.1f;assert(!vr_surface_detail(detail,4,door,4));detail[0].z-=.6f;
    for(int i=0;i<4;i++)detail[i].z+=.1f;
    assert(!vr_surface_detail(detail,4,door,4)); /* A distinct raised panel. */
    for(int i=0;i<4;i++)detail[i].z-=.1f;
    detail[0].x=-1;assert(!vr_surface_detail(detail,4,door,4));
    const vr_skid_point adjacent[3]={{64,0,10},{80,0,10},{64,32,10}};
    assert(!vr_surface_detail(adjacent,3,door,4));
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
        assert(fabsf(p.y-p.x*.1f)<.0001f); /* exact receiving surface; raster bias handles depth */
    }
    total=a.total;
    vr_skid_point airborne[4];memcpy(airborne,q,sizeof q);
    for(int i=0;i<4;i++)airborne[i].y+=20;
    vr_skids_project(&a,airborne,slope,0x600);assert(a.total==total);
    const vr_skid_point wall[3]={{0,0,0},{0,100,0},{0,0,100}};
    vr_skids_project(&a,q,wall,0x600);assert(a.total==total);
    const vr_skid_point outside[4]={{-20,0,0},{-10,0,0},{-10,0,10},{-20,0,10}};
    vr_skids_project(&a,outside,slope,0x600);assert(a.total==total);
    /* One new ribbon crosses a road crease: both facets retain their own
       receiving plane and the union neither loses nor doubles its footprint. */
    vr_skids_reset(&a);
    const vr_skid_point crease[2][3]={{{0,0,0},{10,0,0},{0,0,10}},{{10,0,0},{10,2,10},{0,0,10}}};
    const vr_skid_point ribbon[4]={{3,0,3},{7,0,3},{7,.8,7},{3,0,7}};
    for(int i=0;i<2;i++)vr_skids_project(&a,ribbon,crease[i],0x600);
    double ribbon_area=0;int low=0,high=0;
    for(unsigned i=0;i<a.count;i++) {
        const vr_skid_point *p=a.triangles[i].p;
        float centre=p[0].x+p[0].z+p[1].x+p[1].z+p[2].x+p[2].z;
        int upper=centre>30;low+=!upper;high+=upper;
        for(int v=0;v<3;v++)assert(fabsf(p[v].y-(upper?.2f*(p[v].x+p[v].z-10):0))<.00001);
        ribbon_area+=fabs((p[1].x-p[0].x)*(p[2].z-p[0].z)-(p[2].x-p[0].x)*(p[1].z-p[0].z))*.5;
    }
    assert(low&&high&&fabs(ribbon_area-16)<.0001);
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
