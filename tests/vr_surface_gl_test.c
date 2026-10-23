/* Real-ROM production eye callback regression. Run under Xvfb with the same
   --vr-diagnostic/script arguments as vrde-vr. Readback is confined to this test.
   Compare the production bias to no bias and a stronger bias over moving poses;
   a depth-disabled reference also verifies that real geometry still occludes. */
#define VRDE_VR 1
#include "../src/gl.h"
#include "../src/vr_msaa.h"
#include <assert.h>
static int offset_policy;
static void test_polygon_offset(GLfloat factor,GLfloat units)
{
    if(offset_policy==1)glPolygonOffset(0,0);
    else if(offset_policy==2)glPolygonOffset(factor*2,units*2);
    else glPolygonOffset(factor,units);
    if(offset_policy==3)glDisable(GL_DEPTH_TEST);
}
#define glPolygonOffset test_polygon_offset
#define main vrde_main
#include "../src/vrde.c"
#undef main
#undef glPolygonOffset

int main(int argc,char **argv)
{
    assert(vrde_main(argc,argv)==0);
    assert(vr_scene && vr_scene->decal_first<vr_scene->skid_first);
    enum { W=768,H=768,POSES=24 };
    const vr_msaa_gl_api_t gl={glGetIntegerv,glGenFramebuffers,glDeleteFramebuffers,
        glBindFramebuffer,glCheckFramebufferStatus,glGenRenderbuffers,glDeleteRenderbuffers,
        glBindRenderbuffer,glRenderbufferStorageMultisample,glGetRenderbufferParameteriv,
        glFramebufferRenderbuffer,glDrawBuffer,glReadBuffer,glIsEnabled,glEnable,glDisable,
        glBlitFramebuffer,NULL};
    vr_msaa_target_t target={0};assert(vr_msaa_target_create(&gl,&target,W,H));
    GLuint resolved=create_offscreen_framebuffer(W,H);
    static unsigned char pixels[4][W*H*4];
    unsigned long changed[3]={0};unsigned minimum=~0u,maximum=0;
    XrFovf fov={-.785398f,.785398f,.785398f,-.785398f};
    for(int pose_index=0;pose_index<POSES;pose_index++) {
        float t=pose_index/(float)(POSES-1);
        XrQuaternionf q=vr_yaw((180+(t-.5f)*4)*M_PI/180);
        XrPosef pose={q,vr_rotate(q,(XrVector3f){-.032f+(t-.5f)*.04f,(t-.5f)*.02f,0})};
        for(offset_policy=0;offset_policy<4;offset_policy++) {
            glBindFramebuffer(GL_FRAMEBUFFER,target.framebuffer);
            vr_eye(0,pose,fov,W,H);
            GLboolean writes=0;glGetBooleanv(GL_DEPTH_WRITEMASK,&writes);
            assert(writes && !glIsEnabled(GL_POLYGON_OFFSET_FILL));
            assert(vr_msaa_target_resolve(&gl,&target,resolved,W,H));
            glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels[offset_policy]);
            assert(glGetError()==GL_NO_ERROR);
        }
        unsigned unbiassed=0,fixed=0,occluded=0;
        for(int p=0;p<W*H;p++) {
            unbiassed+=memcmp(pixels[1]+4*p,pixels[2]+4*p,3)!=0;
            fixed+=memcmp(pixels[0]+4*p,pixels[2]+4*p,3)!=0;
            occluded+=memcmp(pixels[3]+4*p,pixels[2]+4*p,3)!=0;
        }
        if(unbiassed<minimum)minimum=unbiassed;
        if(unbiassed>maximum)maximum=unbiassed;
        changed[0]+=unbiassed;changed[1]+=fixed;changed[2]+=occluded;
        printf("surface-gl pose=%d unbiased_diff=%u fixed_diff=%u occluded=%u\n",pose_index,unbiassed,fixed,occluded);
    }
    printf("surface-gl poses=%d samples=%d decals=%d unbiased_total=%lu fixed_total=%lu occluded_total=%lu unbiased_range=%u..%u\n",
        POSES,VR_EYE_MSAA_SAMPLES,(vr_scene->count-vr_scene->decal_first)/3,
        changed[0],changed[1],changed[2],minimum,maximum);
    assert(changed[0]>0 && changed[1]*20<changed[0]);
    assert(changed[2]>0); /* Depth-disabled overlays bleed through real occluders. */
    vr_msaa_target_destroy(&gl,&target);
    return 0;
}
