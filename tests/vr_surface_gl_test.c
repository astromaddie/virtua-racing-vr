/* Real-ROM production eye callback regression. Run under Xvfb with the same
   --vr-diagnostic/script arguments as vrde-vr. Readback is confined to this test.
   Compare the production bias to no bias and a stronger bias over moving poses;
   a depth-disabled reference also verifies that real geometry still occludes. */
#define VRDE_VR 1
#include "../src/gl.h"
#include "../src/vr_msaa.h"
#include <assert.h>
static int offset_policy,surface_layer;
static void test_draw_arrays(GLenum mode,GLint first,GLsizei count);
static void test_polygon_offset(GLfloat factor,GLfloat units)
{
    if(offset_policy==1)glPolygonOffset(0,0);
    else if(offset_policy==2)glPolygonOffset(factor*2,units*2);
    else glPolygonOffset(factor,units);
    if(offset_policy==3)glDisable(GL_DEPTH_TEST);
}
#define glPolygonOffset test_polygon_offset
#define glDrawArrays test_draw_arrays
#define main vrde_main
#include "../src/vrde.c"
#undef main
#undef glPolygonOffset
#undef glDrawArrays

static void test_draw_arrays(GLenum mode,GLint first,GLsizei count)
{
    if(vr_scene && mode==GL_TRIANGLES && first==vr_scene->decal_first) {
        if(offset_policy==4)return;
        if(surface_layer==1){first=vr_scene->skid_first;count=vr_scene->count-first;}
    }
    glDrawArrays(mode,first,count);
}

int main(int argc,char **argv)
{
    assert(vrde_main(argc,argv)==0);
    assert(vr_scene && vr_scene->decal_first<vr_scene->skid_first);
    printf("surface-gl vendor=%s renderer=%s version=%s opaque=%d original_overlays=%d ribbons=%d\n",
        glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION),
        vr_scene->decal_first/3,(vr_scene->skid_first-vr_scene->decal_first)/3,(vr_scene->count-vr_scene->skid_first)/3);
    enum { W=768,H=768,POSES=24 };
    const vr_msaa_gl_api_t gl={glGetIntegerv,glGenFramebuffers,glDeleteFramebuffers,
        glBindFramebuffer,glCheckFramebufferStatus,glGenRenderbuffers,glDeleteRenderbuffers,
        glBindRenderbuffer,glRenderbufferStorageMultisample,glGetRenderbufferParameteriv,
        glFramebufferRenderbuffer,glDrawBuffer,glReadBuffer,glIsEnabled,glEnable,glDisable,
        glBlitFramebuffer,NULL};
    vr_msaa_target_t target={0};assert(vr_msaa_target_create(&gl,&target,W,H));
    GLuint resolved=create_offscreen_framebuffer(W,H);
    static unsigned char pixels[5][W*H*4];
    unsigned long changed[2][3]={{0}},coverage[2]={0};unsigned minimum=~0u,maximum=0;
    XrFovf fov={-.785398f,.785398f,.785398f,-.785398f};
    for(int pose_index=0;pose_index<POSES;pose_index++) {
        float t=pose_index/(float)(POSES-1);
        XrQuaternionf q=vr_yaw((vr_env("VRDE_SURFACE_YAW",180,-360,360)+(t-.5f)*4)*M_PI/180);
        XrPosef pose={q,vr_rotate(q,(XrVector3f){-.032f+(t-.5f)*.04f,(t-.5f)*.02f,0})};
        pose.position.x+=vr_env("VRDE_SURFACE_X",0,-2000,2000);
        pose.position.y+=vr_env("VRDE_SURFACE_Y",0,-2000,2000);
        pose.position.z+=vr_env("VRDE_SURFACE_Z",0,-2000,2000);
        for(surface_layer=0;surface_layer<2;surface_layer++) {
        for(offset_policy=0;offset_policy<5;offset_policy++) {
            glBindFramebuffer(GL_FRAMEBUFFER,target.framebuffer);
            vr_eye(0,pose,fov,W,H);
            GLboolean writes=0;glGetBooleanv(GL_DEPTH_WRITEMASK,&writes);
            assert(writes && !glIsEnabled(GL_POLYGON_OFFSET_FILL));
            assert(vr_msaa_target_resolve(&gl,&target,resolved,W,H));
            glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels[offset_policy]);
            assert(glGetError()==GL_NO_ERROR);
            if(pose_index==POSES/2 && (offset_policy==0||offset_policy==1))
                vr_dump_image(surface_layer?(offset_policy?"ribbons-unbiased.bmp":"ribbons-fixed.bmp"):
                    (offset_policy?"surface-unbiased.bmp":"surface-fixed.bmp"),W,H);
        }
        unsigned unbiassed=0,fixed=0,occluded=0;
        for(int p=0;p<W*H;p++) {
            unbiassed+=memcmp(pixels[1]+4*p,pixels[2]+4*p,3)!=0;
            fixed+=memcmp(pixels[0]+4*p,pixels[2]+4*p,3)!=0;
            occluded+=memcmp(pixels[3]+4*p,pixels[2]+4*p,3)!=0;
            coverage[surface_layer]+=memcmp(pixels[0]+4*p,pixels[4]+4*p,3)!=0;
        }
        if(unbiassed<minimum)minimum=unbiassed;
        if(unbiassed>maximum)maximum=unbiassed;
        changed[surface_layer][0]+=unbiassed;changed[surface_layer][1]+=fixed;changed[surface_layer][2]+=occluded;
        printf("surface-gl layer=%d pose=%d unbiased_diff=%u fixed_diff=%u occluded=%u\n",surface_layer,pose_index,unbiassed,fixed,occluded);
        }
    }
    for(int layer=0;layer<2;layer++) {
        printf("surface-gl layer=%d poses=%d samples=%d decals=%d coverage=%lu unbiased_total=%lu fixed_total=%lu occluded_total=%lu unbiased_range=%u..%u\n",
            layer,POSES,VR_EYE_MSAA_SAMPLES,(vr_scene->count-vr_scene->decal_first)/3,coverage[layer],
            changed[layer][0],changed[layer][1],changed[layer][2],minimum,maximum);
        if(coverage[layer]&&changed[layer][0])assert(changed[layer][1]*20<changed[layer][0]);
    }
    assert(coverage[0]>0&&changed[0][0]>0);
    assert(changed[0][2]>0); /* Depth-disabled overlays bleed through real occluders. */
    vr_msaa_target_destroy(&gl,&target);
    return 0;
}
