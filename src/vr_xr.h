#ifndef VRDE_VR_XR_H
#define VRDE_VR_XR_H
#include <SDL.h>
#include <openxr/openxr.h>
#include "vr_cpu.h"
typedef struct {
    double simulation, audio, frame_upload, mesh, vertex_upload, hud;
    unsigned ticks, vertices;
} vr_prepare_timing_t;
typedef struct {
    uint16_t buttons;
    float steer, throttle, brake;
    int focused;
    vr_prepare_timing_t *timing; /* Optional CPU-only recorder; never advances state. */
} vr_input_t;
typedef struct {
    /* Return 1 for immersive racing, 0 for the original flat screen, -1 on failure. */
    int (*prepare)(double time, double period, int active, const vr_input_t *input);
    void (*eye)(int eye, XrPosef pose, XrFovf fov, int width, int height);
    void (*resolved_eye)(int eye, XrPosef pose, int width, int height);
    void (*panel)(int width, int height, int immersive);
    long (*simulation_frames)(void);
} vr_callbacks_t;
int vr_xr_run(SDL_Window *window, const vr_callbacks_t *callbacks);
float vr_env(const char *name, float fallback, float low, float high);
#endif
