/* Optional integration check using the user's ROM, with no ROM/snapshot fixtures.
   Include HLE to verify its private race cache and production snapshot hooks. */
#define VRDE_VR 1
#include "../src/hle.c"
#include "../src/core.h"
#include <assert.h>

static void comm(int writer,unsigned pc,unsigned offset,unsigned v,int size)
{ (void)pc; hle_on_comm_write(writer,offset,v,size); }
static void dreq(int slave,unsigned address,unsigned v)
{ (void)slave; (void)v; hle_on_dreq_write(address); }
static uint64_t hash(const void *data,size_t n)
{
    const uint8_t *p=data;uint64_t h=1469598103934665603ULL;
    while(n--){h^=*p++;h*=1099511628211ULL;}return h;
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc<2){fprintf(stderr,"usage: %s ROM [pause|corner]\n",argv[0]);return 2;}
    int pause=argc>2&&!strcmp(argv[2],"pause");
    int corner=argc>2&&!strcmp(argv[2],"corner");
    FILE *script=fopen(corner?"tools/skid_corner_inputs.txt":"tools/race_inputs.txt","r");assert(script);
    char line[4096];assert(fgets(line,sizeof line,script));fclose(script);
    int frames[128],length=0;uint16_t buttons[128];
    for(char *token=strtok(line," \n");token;token=strtok(NULL," \n")) {
        assert(!strcmp(token,"-p"));token=strtok(NULL," \n");assert(token);
        char *colon=strchr(token,':');assert(colon);*colon=0;
        frames[length]=atoi(token);buttons[length++]=core_parse_buttons(colon+1);
    }
    assert(core_init(argv[1],48000)==0);
    vrde_comm_write_hook=comm;vrde_dreq_write_hook=dreq;
    unsigned pad=0,peak=0,peak_player=0,peak_ai=0,at_pause=0;
    unsigned first_count=0;vr_skid_triangle first={0};
    int old_scene=0,first_frame=-1,sound_eligible=0,aligned_sound=0;
    for(int f=0;f<(pause?2800:8000);f++) {
        for(int i=0;i<length;i++)if(f==frames[i])pad=buttons[i];
        if(pause) {
            if(f==2600||f==2700)pad=core_parse_buttons("BS");
            if(f==2606||f==2706)pad=core_parse_buttons("B");
        }
        vr_skid_sample old=skid_marks.cars[0].previous;
        unsigned old_emitted=skid_marks.emitted[0];
        core_run_frame(pad);hle_on_frame_end();
        if(old_scene!=scenes_taken) {
            old_scene=scenes_taken;
            const vr_skid_sample *v=&skid_marks.cars[0].previous;
            if(abs(v->sound_slip)>32)sound_eligible++;
            if(skid_marks.count && first_frame<0){first_frame=f;first=skid_marks.triangles[0];}
            if(skid_marks.count>peak)peak=skid_marks.count;
            if(skid_marks.emitted[0]>peak_player)peak_player=skid_marks.emitted[0];
            unsigned ai=0;for(int i=1;i<16;i++)ai+=skid_marks.emitted[i];
            if(ai>peak_ai)peak_ai=ai;
            if(old.valid && abs(v->sound_slip)>32 && skid_marks.emitted[0]>old_emitted) {
                float dx=v->centre.x-old.centre.x,dz=v->centre.z-old.centre.z;
                float yaw=old.yaw+remainderf(v->yaw-old.yaw,2*M_PI)*.5f;
                float slip=fabsf(dx*cosf(yaw)+dz*sinf(yaw))/hypotf(dx,dz);
                if(slip<.18f) {
                    if(!aligned_sound)printf("skid-rom sound_corner frame=%d original_slip=%d geometric_slip=%.4f emitted=%u\n",
                        f,v->sound_slip,slip,skid_marks.emitted[0]-old_emitted);
                    aligned_sound++;
                }
            }
        }
        if(pause&&f==2610)at_pause=skid_marks.count;
        if(pause&&f==2690)assert(at_pause>0&&skid_marks.count==at_pause);
        if(f==2464||f==3000||f==3200||f==7400) {
            uint64_t before=hash(&skid_marks,sizeof skid_marks);
            const hle_vr_mesh_t *mesh=hle_vr_prepare(.25);
            assert(mesh&&!mesh->overflow);
            uint64_t vertices=hash(mesh->vertices,mesh->count*sizeof(*mesh->vertices));
            assert(hle_vr_prepare(.25)==mesh);
            assert(vertices==hash(mesh->vertices,mesh->count*sizeof(*mesh->vertices)));
            assert(before==hash(&skid_marks,sizeof skid_marks));
            assert(skid_marks.count>0);
            assert(!memcmp(&first,&skid_marks.triangles[0],sizeof first));
            printf("skid-rom frame=%d stored=%u player=%u ai=%u drawn=%d sound=%d\n",f,
                   skid_marks.count,skid_marks.emitted[0],skid_marks.total-skid_marks.emitted[0],
                   (mesh->count-mesh->skid_first)/3,skid_marks.cars[0].previous.sound_slip);
            if(f==2464)first_count=skid_marks.count;
            if(f==3200){assert(snapshot_word(&snapshots[1],PLAYER_CAR+FLAG)==2);assert(skid_marks.count>=first_count);}
        }
    }
    assert(peak_player>0 && sound_eligible>0);
    if(!pause&&!corner)assert(peak_ai>0);
    if(!pause&&!corner)assert(skid_marks.count==0); /* original results HUD resets the race */
    hle_reset();assert(skid_marks.count==0 && snapshot_count==0 && skid_grid==0);
    printf("skid-rom PASS first_frame=%d peak=%u player=%u ai=%u sound_ticks=%d early_sound=%d pause=%d\n",
           first_frame,peak,peak_player,peak_ai,sound_eligible,aligned_sound,pause);
    if(corner)assert(aligned_sound>0);
}
