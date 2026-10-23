#ifndef VRDE_VR_CPU_H
#define VRDE_VR_CPU_H
/* Main thread only. Scope synchronous CPU work; leave before any SDL/GL/XR
   operation that could lazily create a thread and inherit its caller's mask. */
void vr_cpu_init(int enabled);
void vr_cpu_enter(void);
int vr_cpu_leave(void); /* 0: restoration failed; stop before external calls. */
void vr_cpu_shutdown(void);
#endif
