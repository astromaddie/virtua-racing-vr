#ifndef VRDE_VR_MATH_H
#define VRDE_VR_MATH_H
#include <openxr/openxr.h>
/* OpenGL column-major matrices. Mesh source is +Z forward; XR is -Z forward. */
void vr_matrix(float out[16], XrPosef eye, XrFovf fov, float units_per_metre, float pitch, float roll);
XrVector3f vr_rotate(XrQuaternionf q, XrVector3f v);
XrQuaternionf vr_yaw(float radians);
XrPosef vr_relative_pose(XrPosef pose, XrPosef origin);
#endif
