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
static unsigned checked, contacts, banked;
static float worst_before=INFINITY,worst_after=INFINITY;
static int support(vr_skid_point p,float *height,float *slope)
{
    int col=16+(int)floorf(p.x/1024),row=15-(int)floorf(p.z/1024),found=0;
    float nearest=1e9;
    for(int y=row-1;y<=row+1;y++)for(int x=col-1;x<=col+1;x++) {
        if(x<0||x>=32||y<0||y>=32)continue;
        int cell=y*32+x;skid_load_cell(cell);
        for(unsigned i=skid_cells[cell].first;i<skid_cells[cell].first+skid_cells[cell].count;i++) {
            const vr_skid_point *r=skid_roads[i].p;
            double ux=r[1].x-r[0].x,uy=r[1].y-r[0].y,uz=r[1].z-r[0].z;
            double vx=r[2].x-r[0].x,vy=r[2].y-r[0].y,vz=r[2].z-r[0].z;
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            if(ny*ny<.64*(nx*nx+ny*ny+nz*nz))continue;
            double a=-ny;
            double b=((r[1].x-p.x)*(r[2].z-p.z)-(r[2].x-p.x)*(r[1].z-p.z))/a;
            double c=((r[2].x-p.x)*(r[0].z-p.z)-(r[0].x-p.x)*(r[2].z-p.z))/a;
            double e=1-b-c;if(b<-.00001||c<-.00001||e<-.00001)continue;
            double h=r[0].y-(nx*(p.x-r[0].x)+nz*(p.z-r[0].z))/ny;
            if(fabs(p.y-h)<nearest) { nearest=fabs(p.y-h);*height=h;*slope=hypot(nx,nz)/fabs(ny);found=1; }
        }
    }
    return found&&nearest<80;
}

static void check_car_records(void)
{
    vr_collect=1;build_matrices();
    matrix_t view=view_matrix();
    /* Independently intersect the uncorrected wheel vertices with ROM asphalt,
       then measure the actual display translation against those planes. */
    for(int car=0;car<16;car++) {
        int body=car?OPPONENTS+(car-1)*OPPONENT_SIZE:PLAYER_CAR;
        if(!field(body,FLAG)||(!car&&field(body,FLAG)==2))continue;
        float gap=INFINITY,slope=0;
        for(int part=1;part<=(car?2:3);part++) {
            int r=body+part*RECORD_SIZE;if(!field(r,FLAG))continue;
            uint32_t model=scene_long(r+MODEL);
            const struct wheel_model *shape=vr_wheel_model(model);
            if(!shape||shape->groups<1||shape->groups>2)fprintf(stderr,"wheel topology record=%x model=%x groups=%d count=%d\n",r,model,shape?shape->groups:-1,shape?shape->count:-1);
            assert(shape&&shape->groups>=1&&shape->groups<=2);
            geometry_t g;start_geometry(&g,0);g.matrix=object_matrix[record_index(r)];
            vr_mesh.count=vr_decal_vertices=0;draw_model(&g,model);
            for(int i=0;i<vr_mesh.count;i++) {
                const hle_vr_vertex_t *point=&vr_mesh.vertices[i];
                vertex_t v={0};v.fx=point->x/16.;v.fy=point->y/16.;v.fz=point->z/16.;
                double camera[3]={v.fx-view.f[0][3],v.fy-view.f[1][3],v.fz-view.f[2][3]},w[3]={0};
                for(int a=0;a<3;a++)for(int b=0;b<3;b++)w[a]+=view.f[b][a]*camera[b]*16;
                float height,tilt;
                if(support((vr_skid_point){w[0],w[1],w[2]},&height,&tilt)) {
                    gap=fminf(gap,w[1]-height);slope=fmaxf(slope,tilt);
                }
            }
        }
        if(!isfinite(gap))continue;
        float lowering=0;for(int a=0;a<3;a++)lowering+=view.f[a][1]*vr_mesh.car_lowering[car][a];
        float after=gap-lowering;
        assert(after>=-.02f); /* Real wheel/receiving-plane clearance, not an offset oracle. */
        assert(lowering<=4.001f); /* No pulling an airborne car onto the road. */
        if(gap>4)assert(fabsf(after-(gap-4))<.002f);
        contacts++;if(slope>.1f&&gap<4)banked++;
        worst_before=fminf(worst_before,gap-4);worst_after=fminf(worst_after,after);
    }
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
            /* The complete body/children/effects retain their relative positions. */
            int car=record<OPPONENTS?0:1+(record-OPPONENTS)/OPPONENT_SIZE;
            const float *move=vr_mesh.car_lowering[car];
            assert(fabsf(dx-move[0])<.01f&&fabsf(dy-move[1])<.01f&&fabsf(dz-move[2])<.01f);
            assert(a->colour==b->colour);checked++;
        }
    }
    vr_collect=0;
}
/* Independent area/plane oracle over decoded ROM faces and emitted cache
   triangles. It does not call the production clipping/projection helpers. */
static double area_xz(vr_skid_point a,vr_skid_point b,vr_skid_point c)
{ return fabs((b.x-a.x)*(double)(c.z-a.z)-(b.z-a.z)*(double)(c.x-a.x))*.5; }
static int on_plane(const vr_skid_point p[3],const vr_skid_point base[3])
{
    double u[3]={base[1].x-base[0].x,base[1].y-base[0].y,base[1].z-base[0].z};
    double v[3]={base[2].x-base[0].x,base[2].y-base[0].y,base[2].z-base[0].z};
    double n[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    double length=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if(length<.01)return 0;
    for(int i=0;i<3;i++)if(fabs(n[0]*(p[i].x-base[0].x)+n[1]*(p[i].y-base[0].y)+n[2]*(p[i].z-base[0].z))>.004*length)return 0;
    return 1;
}
static unsigned repair_triangles_checked,repair_road_faces;
static void check_repair(int cell,unsigned polygon,const vr_skid_point *source,int corners)
{
    int replacement=vr_surface_replacement(cell,polygon);if(replacement<0)return;
    double expected=0,actual=0;
    for(int k=1;k<corners-1;k++)expected+=area_xz(source[0],source[k],source[k+1]);
    double ux=source[1].x-source[0].x,uy=source[1].y-source[0].y,uz=source[1].z-source[0].z;
    double vx=source[2].x-source[0].x,vy=source[2].y-source[0].y,vz=source[2].z-source[0].z;
    double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
    if(expected<.01||ny*ny<.64*(nx*nx+ny*ny+nz*nz))return; /* Vertical insets have a separate authenticated oracle below. */
    unsigned first=surface_replacements[replacement].first,count=surface_replacements[replacement].count;
    for(unsigned i=first;i<first+count;i++) {
        const struct surface_triangle *t=&surface_triangles[i];actual+=area_xz(t->p[0],t->p[1],t->p[2]);
        int authored=0,receiver=0;
        for(int k=1;k<corners-1;k++) {
            vr_skid_point face[3]={source[0],source[k],source[k+1]};
            authored|=on_plane(t->p,face);
        }
        if(t->decal)for(unsigned r=skid_cells[cell].first;r<skid_cells[cell].first+skid_cells[cell].count;r++) {
            const vr_skid_point *road=skid_roads[r].p;
            if(!on_plane(t->p,road))continue;
            vr_skid_point centre={(t->p[0].x+t->p[1].x+t->p[2].x)/3,0,(t->p[0].z+t->p[1].z+t->p[2].z)/3};
            double sum=area_xz(road[0],road[1],centre)+area_xz(road[1],road[2],centre)+area_xz(road[2],road[0],centre);
            if(sum<=area_xz(road[0],road[1],road[2])+1)receiver=1;
        }
        assert(t->decal?(receiver||authored):authored);
        repair_triangles_checked++;
    }
    /* Splitting may neither erase an authored outside portion nor grow paint. */
    if(fabs(actual-expected)>=fmax(.2,expected*.0001))fprintf(stderr,"repair area grid=%x cell=%d polygon=%u expected=%g actual=%g\n",skid_grid,cell,polygon,expected,actual);
    assert(fabs(actual-expected)<fmax(.2,expected*.0001));repair_road_faces++;
}
static void check_courses(void)
{
    for(int course=0;course<5;course++) {
        skid_reset();skid_grid=course_grids[course];unsigned counts[256]={0},total=0;
        for(int cell=0;cell<1024;cell++) {
            uint32_t model=grid_cell(skid_grid,cell);if(model==GRID_EMPTY)continue;
            skid_load_cell(cell);int n=sh2_word(model);model+=4;
            static geometry_t g;memset(&g,0,sizeof(g));
            for(int i=0;i<n;i++,model+=6) {
                g.vertices[i].fx=sh2_signed_word(model)*.5;
                g.vertices[i].fy=sh2_signed_word(model+2)*.5;
                g.vertices[i].fz=sh2_signed_word(model+4)*.5;
            }
            for(unsigned poly=0;poly<2048;poly++) {
                uint16_t header=sh2_word(model);model+=2;if(((header>>8)&14)==12)break;
                load_polygon(&g,header,&model,1);unsigned bits=vr_road_decal(cell,poly);
                unsigned count=(bits&1)+((bits>>1)&1);counts[g.colour>>8]+=count;total+=count;
                vr_skid_point face[4];int corners=(header&1)?3:4;
                for(int k=0;k<corners;k++)face[k]=(vr_skid_point){g.slots[k].fx,g.slots[k].fy,g.slots[k].fz};
                check_repair(cell,poly,face,corners);
                int replacement=vr_surface_replacement(cell,poly);
                if(replacement>=0) {
                    unsigned colour=(header&0x20)?lit_colour(&g,g.colour):g.colour;
                    vr_mesh.count=vr_decal_vertices=vr_mesh.overflow=0;
                    vr_polygon(&g,header,corners,bits,replacement);
                    assert(!vr_mesh.overflow);
                    assert(vr_mesh.count+vr_decal_vertices==3*(int)surface_replacements[replacement].count);
                    for(int v=0;v<vr_mesh.count;v++)assert(vr_mesh.vertices[v].colour==colour);
                    for(int v=HLE_VR_MAX_VERTICES-vr_decal_vertices;v<HLE_VR_MAX_VERTICES;v++)assert(vr_mesh.vertices[v].colour==colour);
                }
                if(course==0&&cell==19*32+12&&poly==48) {
                    assert(g.colour==0x0500);int r=vr_surface_replacement(cell,poly);assert(r>=0);
                    double area=0;
                    for(unsigned t=surface_replacements[r].first;t<surface_replacements[r].first+surface_replacements[r].count;t++) {
                        const vr_skid_point *p=surface_triangles[t].p;
                        for(int v=0;v<3;v++){assert(fabs(p[v].x+3897.5)<.001);assert(p[v].y>=160&&p[v].y<=224);assert(p[v].z>=-3888&&p[v].z<=-3648);}
                        area+=fabs((p[1].y-p[0].y)*(double)(p[2].z-p[0].z)-(p[2].y-p[0].y)*(double)(p[1].z-p[0].z))*.5;
                    }
                    assert(fabs(area-64*240)<.001);
                }
            }
        }
        assert(total>0 && skid_road_count<SKID_ROAD_CAPACITY);
        assert(surface_triangle_count<SURFACE_TRIANGLES&&surface_replacement_count<SURFACE_REPLACEMENTS);
        if(course==0) {
            assert(grid_cell(skid_grid,18*32+12)==0x220B0478);
            assert(vr_surface_replacement(18*32+12,45)>=0);
            assert(vr_surface_replacement(19*32+12,48)>=0);
            for(int p=56;p<=58;p++)assert(vr_surface_replacement(20*32+12,p)>=0);
        }
        printf("surface-rom repaired_polygons=%u repaired_triangles=%u\n",surface_replacement_count,surface_triangle_count);
        /* Exact known garage-plane provenance, including details authored on
           either side of their base in record order. */
        if(course==1) {
            int cell=19*32+20;assert(grid_cell(skid_grid,cell)==0x220D0118);
            assert(vr_road_decal(cell,57)==1&&vr_road_decal(cell,59)==3);
            assert(vr_road_decal(cell,62)==1&&vr_road_decal(cell,63)==3);
            assert(!vr_road_decal(cell,58)&&!vr_road_decal(cell,61));
        }
        printf("surface-rom grid=%x roads=%u decals=%u materials=",skid_grid,skid_road_count,total);
        for(int i=0;i<256;i++)if(counts[i])printf("%d:%u,",i,counts[i]);
        puts("");
    }
}
int main(int argc,char **argv)
{
    assert(argc>=2);int kind=argc>2?atoi(argv[2]):0;assert(kind>=0&&kind<3);
    assert(core_init(argv[1],48000)==0);vrde_comm_write_hook=comm;vrde_dreq_write_hook=dreq;
    hle_set_detailed_opponents(1);hle_set_far_view(1);
    FILE *file=fopen("tools/skid_corner_inputs.txt","r");assert(file);
    char line[4096];assert(fgets(line,sizeof line,file));fclose(file);
    int frames[128],length=0;uint16_t buttons[128];
    for(char *p=strtok(line," \n");p;p=strtok(NULL," \n")) {
        assert(!strcmp(p,"-p"));p=strtok(NULL," \n");assert(p);char *colon=strchr(p,':');assert(colon);*colon=0;
        frames[length]=atoi(p);buttons[length++]=core_parse_buttons(colon+1);
    }
    unsigned pad=0;
    for(int frame=0;frame<7022;frame++) {
        for(int i=0;i<length;i++)if(frame==frames[i])pad=buttons[i];
        if(kind&&frame==1000)pad=core_parse_buttons(kind==1?"R":"L");
        if(kind&&frame==1006)pad=0;
        core_run_frame(pad);hle_on_frame_end();
        if(frame==1801||frame==2465||frame==2970||frame==4410||frame==6600||frame==7020) {
            for(int half=0;half<2;half++) {hle_vr_prepare(half*.5);check_car_records();}
            printf("surface-rom kind=%d frame=%d pitch=%d roll=%d corrected_vertices=%u\n",kind,frame,field(PLAYER_CAR,ANGLE_X),field(PLAYER_CAR,ANGLE_Z),checked);
            if(frame==1801) {
                /* Controlled airborne delta on a real opponent assembly. Only
                   the HLE scratch scene changes, never emulated RAM or physics. */
                set_scene_word(OPPONENTS+POS_Y,scene_word(OPPONENTS+POS_Y)+1600);
                exact_from_scene();vr_collect=1;build_matrices();vr_car_contacts();check_car_records();
                matrix_t view=view_matrix();float lowering=0;
                for(int a=0;a<3;a++)lowering+=view.f[a][1]*vr_mesh.car_lowering[1][a];
                assert(fabsf(lowering-4)<.001f);
                printf("surface-rom kind=%d raised_opponent_world_y=100 remains_airborne\n",kind);
            }
        }
    }
    assert(checked>1000&&contacts>50&&banked>0);
    printf("surface-rom contacts=%u banked=%u baseline_min_gap=%.5f corrected_min_gap=%.5f\n",contacts,banked,worst_before,worst_after);
    for(unsigned i=0;i<wheel_model_count;i++)printf("surface-rom wheel=%x rendered_vertices=%d tyre_components=%d\n",wheel_models[i].address,wheel_models[i].count,wheel_models[i].groups);
    if(!kind){check_courses();printf("surface-rom independent repaired road faces=%u triangles=%u\n",repair_road_faces,repair_triangles_checked);}
    puts("surface-rom PASS");return 0;
}
