/* Presentation-only tyre trails. All coordinates are final game world units. */
#ifndef VRDE_SKIDS_H
#define VRDE_SKIDS_H
#include <stdint.h>
#define VR_SKID_CARS 16
#define VR_SKID_CAPACITY 8192

typedef struct { float x, y, z; } vr_skid_point;
typedef struct { vr_skid_point p[3]; uint32_t colour; int frame; } vr_skid_triangle;
typedef struct {
    vr_skid_point centre, tyre[2];
    float yaw, width[2];
    int valid;
    int sound_slip; /* Original car +0x94: sound BD above32, C8 at/below16. */
} vr_skid_sample;
typedef struct {
    vr_skid_sample previous;
    unsigned episode;
    int frame, warm, sliding, selected, sound_active;
} vr_skid_car;
typedef struct {
    vr_skid_triangle triangles[VR_SKID_CAPACITY];
    vr_skid_car cars[VR_SKID_CARS];
    unsigned count, next, total, emitted[VR_SKID_CARS];
    int frame;
} vr_skids;
typedef void (*vr_skid_project)(void *user, const vr_skid_point quad[4], unsigned colour);
/* Chronological iteration keeps the newest shade on top after ring wrap. */
static inline const vr_skid_triangle *vr_skids_triangle(const vr_skids *s,unsigned i)
{
    unsigned oldest=s->count==VR_SKID_CAPACITY?s->next:0;
    return &s->triangles[(oldest+i)%VR_SKID_CAPACITY];
}
void vr_skids_reset(vr_skids *s);
void vr_skids_cut(vr_skids *s);
/* Called once per original snapshot, never from an eye/display pass. */
void vr_skids_step(vr_skids *s, int car, int frame, const vr_skid_sample *sample,
                   vr_skid_project project, void *user);
/* Clip the entire ribbon to a real road triangle, including its slope. Reject
   non-contact, vertical faces and degenerate geometry; no marks over the edge. */
void vr_skids_project(vr_skids *s, const vr_skid_point quad[4],
                      const vr_skid_point road[3], unsigned colour);
#endif
