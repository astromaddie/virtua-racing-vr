/* Optional real-ROM material and complete-car transform checks. No ROM fixtures.
   Run from the repository root: build/vr_surface_rom_test ROM [kind 0..2]. */
#define VRDE_VR 1
#include "../src/hle.c"
#include "../src/core.h"
#include <assert.h>
static void comm(int writer,unsigned pc,unsigned o,unsigned v,int size)
{ (void)pc;hle_on_comm_write(writer,o,v,size); }
static void dreq(int slave,unsigned address,unsigned v)
{ (void)slave;(void)v;hle_on_dreq_write(address); }
static hle_vr_vertex_t reference[8192];
static unsigned checked;
static void check_car_records(void)
{
    vr_collect=1;build_matrices();
    matrix_t view=view_matrix();
    for(int record=PLAYER_CAR;record<ROADSIDE;record+=RECORD_SIZE) {
        if(!field(record,FLAG))continue;
        geometry_t g;start_geometry(&g,0);
        g.matrix=object_matrix[record_index(record)];
        vr_decal_vertices=vr_mesh.count=vr_mesh.overflow=0;
        draw_model(&g,scene_long(record+MODEL));
        int n=vr_mesh.count;assert(n<=8192 && !vr_mesh.overflow);
        memcpy(reference,vr_mesh.vertices,n*sizeof(*reference));
        vr_mesh.count=0;start_geometry(&g,0);draw_records(&g,record,1);
        assert(vr_mesh.count==n);
        for(int i=0;i<n;i++) {
            const hle_vr_vertex_t *a=&reference[i],*b=&vr_mesh.vertices[i];
            float dx=a->x-b->x,dy=a->y-b->y,dz=a->z-b->z;
            /* Independent inverse rotation: every corner moves only down in
               world Y by four units, irrespective of car pitch, wheel spin or
               airborne position. Model shape and part offsets remain rigid. */
            for(int axis=0;axis<3;axis++) {
                float world=view.f[0][axis]*dx+view.f[1][axis]*dy+view.f[2][axis]*dz;
                assert(fabsf(world-(axis==1?4.f:0.f))<.01f);
            }
            assert(a->colour==b->colour);checked++;
        }
    }
    vr_collect=0;
}
static void check_courses(void)
{
    for(int course=0;course<5;course++) {
        skid_reset();skid_grid=course_grids[course];unsigned counts[256]={0},total=0;
        for(int cell=0;cell<1024;cell++) {
            uint32_t model=grid_cell(skid_grid,cell);if(model==GRID_EMPTY)continue;
            skid_load_cell(cell);int n=sh2_word(model);model+=4+6*n;
            static geometry_t g;memset(&g,0,sizeof(g));
            for(unsigned poly=0;poly<2048;poly++) {
                uint16_t header=sh2_word(model);model+=2;if(((header>>8)&14)==12)break;
                load_polygon(&g,header,&model,1);unsigned bits=vr_road_decal(cell,poly);
                unsigned count=(bits&1)+((bits>>1)&1);counts[g.colour>>8]+=count;total+=count;
            }
        }
        assert(total>0 && skid_road_count<SKID_ROAD_CAPACITY);
        assert(counts[12]==0 && counts[13]==0 && counts[14]==0);
        printf("surface-rom grid=%x roads=%u decals=%u materials=",skid_grid,skid_road_count,total);
        for(int i=0;i<256;i++)if(counts[i])printf("%d:%u,",i,counts[i]);
        puts("");
    }
}
int main(int argc,char **argv)
{
    assert(argc>=2);int kind=argc>2?atoi(argv[2]):0;assert(kind>=0&&kind<3);
    assert(core_init(argv[1],48000)==0);vrde_comm_write_hook=comm;vrde_dreq_write_hook=dreq;
    FILE *file=fopen("tools/skid_corner_inputs.txt","r");assert(file);
    char line[4096];assert(fgets(line,sizeof line,file));fclose(file);
    int frames[128],length=0;uint16_t buttons[128];
    for(char *p=strtok(line," \n");p;p=strtok(NULL," \n")) {
        assert(!strcmp(p,"-p"));p=strtok(NULL," \n");assert(p);char *colon=strchr(p,':');assert(colon);*colon=0;
        frames[length]=atoi(p);buttons[length++]=core_parse_buttons(colon+1);
    }
    unsigned pad=0;
    for(int frame=0;frame<4202;frame++) {
        for(int i=0;i<length;i++)if(frame==frames[i])pad=buttons[i];
        if(kind&&frame==1000)pad=core_parse_buttons(kind==1?"R":"L");
        if(kind&&frame==1006)pad=0;
        core_run_frame(pad);hle_on_frame_end();
        if(frame==1801||frame==2465||frame==3201||frame==4101) {
            memcpy(scene,snapshots[1].scene,sizeof scene);exact_from_scene();check_car_records();
            printf("surface-rom kind=%d frame=%d pitch=%d roll=%d corrected_vertices=%u\n",kind,frame,field(PLAYER_CAR,ANGLE_X),field(PLAYER_CAR,ANGLE_Z),checked);
            if(frame==1801) {
                /* Controlled airborne delta on a real opponent assembly. Only
                   the HLE scratch scene changes, never emulated RAM or physics. */
                set_scene_word(OPPONENTS+POS_Y,scene_word(OPPONENTS+POS_Y)+1600);
                exact_from_scene();check_car_records();
                printf("surface-rom kind=%d raised_opponent_world_y=100 retains_fixed_correction\n",kind);
            }
        }
    }
    assert(checked>1000);if(!kind)check_courses();
    puts("surface-rom PASS");return 0;
}
