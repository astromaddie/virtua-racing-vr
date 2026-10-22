#include "vr_math.h"
#include <math.h>
#include <string.h>
XrQuaternionf vr_yaw(float a)
{
    return (XrQuaternionf){0, sinf(a / 2), 0, cosf(a / 2)};
}
static XrQuaternionf multiply(XrQuaternionf a, XrQuaternionf b)
{
    return (XrQuaternionf){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrVector3f vr_rotate(XrQuaternionf q, XrVector3f v)
{
    XrQuaternionf c = {-q.x, -q.y, -q.z, q.w}, p = {v.x, v.y, v.z, 0};
    p = multiply(multiply(q, p), c);
    return (XrVector3f){p.x, p.y, p.z};
}
XrPosef vr_relative_pose(XrPosef p, XrPosef o)
{
    XrQuaternionf inverse = {-o.orientation.x, -o.orientation.y, -o.orientation.z, o.orientation.w};
    XrVector3f delta = {p.position.x - o.position.x, p.position.y - o.position.y,
                        p.position.z - o.position.z};
    return (XrPosef){multiply(inverse, p.orientation), vr_rotate(inverse, delta)};
}
static void product(float o[16], const float a[16], const float b[16])
{
    float r[16] = {0};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++) {
                r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
            }
    memcpy(o, r, sizeof(r));
}
void vr_matrix(float out[16], XrPosef eye, XrFovf fov, float scale, float pitch, float roll)
{
    float l = tanf(fov.angleLeft), r = tanf(fov.angleRight), d = tanf(fov.angleDown), u = tanf(fov.angleUp);
    float n = .05f, f = 2000.f;
    float projection[16] = {2 / (r - l),
                            0,
                            0,
                            0,
                            0,
                            2 / (u - d),
                            0,
                            0,
                            (r + l) / (r - l),
                            (u + d) / (u - d),
                            -(f + n) / (f - n),
                            -1,
                            0,
                            0,
                            -2 * f * n / (f - n),
                            0};
    XrQuaternionf inverse = {-eye.orientation.x, -eye.orientation.y, -eye.orientation.z, eye.orientation.w};
    XrVector3f x = vr_rotate(inverse, (XrVector3f){1, 0, 0});
    XrVector3f y = vr_rotate(inverse, (XrVector3f){0, 1, 0});
    XrVector3f z = vr_rotate(inverse, (XrVector3f){0, 0, 1});
    XrVector3f t = vr_rotate(inverse, (XrVector3f){-eye.position.x, -eye.position.y, -eye.position.z});
    float view[16] = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, t.x, t.y, t.z, 1};
    /* Undo native camera pitch/roll in its +Z basis, then change handedness. */
    float cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    float level[16] = {cr / scale,
                       cp * sr / scale,
                       -sp * sr / scale,
                       0,
                       -sr / scale,
                       cp * cr / scale,
                       -sp * cr / scale,
                       0,
                       0,
                       -sp / scale,
                       -cp / scale,
                       0,
                       0,
                       0,
                       0,
                       1};
    float temp[16];
    product(temp, view, level);
    product(out, projection, temp);
}
