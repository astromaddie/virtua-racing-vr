#include "vr_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void close_to(float a, float b)
{
    assert(fabsf(a - b) < .0001f);
}
static void project(const float m[16], float x, float y, float z, float out[3])
{
    float w = m[3] * x + m[7] * y + m[11] * z + m[15];
    for (int i = 0; i < 3; i++)
        out[i] = (m[i] * x + m[4 + i] * y + m[8 + i] * z + m[12 + i]) / w;
}
int main(void)
{
    XrFovf f = {-atanf(160.f / 112), atanf(160.f / 112), atanf(1), -atanf(1)};
    XrPosef p = {{0, 0, 0, 1}, {0, 0, 0}};
    float m[16], v[3];
    vr_matrix(m, p, f, 64, 0, 0);
    project(m, 20, 10, 100, v);
    close_to(v[0], 112.f * 20 / 100 / 160);
    close_to(v[1], 10.f / 100);
    p.position.x = -.032f;
    vr_matrix(m, p, f, 64, 0, 0);
    project(m, 0, 0, 64, v);
    float near = v[0];
    assert(near > 0);
    project(m, 0, 0, 128, v);
    close_to(near, 2 * v[0]);
    p.position.x = .032f;
    vr_matrix(m, p, f, 64, 0, 0);
    project(m, 0, 0, 64, v);
    close_to(v[0], -near);
    p.position.x = 0;
    p.orientation = vr_yaw(M_PI / 2);
    vr_matrix(m, p, f, 64, 0, 0);
    project(m, -64, 0, 0, v);
    close_to(v[0], 0);
    close_to(v[1], 0);
    /* A world-level forward point transformed by native downward pitch must return
       to the horizon after levelling (independent sine/cosine identity). */
    p.orientation = vr_yaw(0);
    float a = .2f;
    vr_matrix(m, p, f, 64, a, 0);
    project(m, 0, sinf(a) * 100, cosf(a) * 100, v);
    close_to(v[1], 0);
    XrPosef origin = {vr_yaw(M_PI / 2), {2, 1, 3}}, head = origin;
    XrPosef relative = vr_relative_pose(head, origin);
    close_to(relative.orientation.w, 1);
    close_to(relative.position.x, 0);
    puts("VR math: native projection, IPD signs/inverse-depth, 90-degree yaw, horizon, recenter PASS");
}
