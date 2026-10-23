/* Linux GLX OpenXR host. The GLX binding follows tools/openxr_colour_probe.c. */
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include "vr_xr.h"
#include "gl.h"
#include "platform/common/input_pico.h"
#include "sfvr/sfvr_view.h"
#include "vr_msaa.h"
#include "vr_math.h"
#include <GL/glx.h>
#include <SDL_syswm.h>
#include <X11/Xlib.h>
#include <math.h>
#include <openxr/openxr_platform.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stopped;
static void stop_signal(int sig)
{
    (void)sig;
    stopped = 1;
}
float vr_env(const char *name, float fallback, float low, float high)
{
    const char *s = getenv(name);
    char *end;
    if (!s)
        return fallback;
    float v = strtof(s, &end);
    return end == s || *end || !isfinite(v) ? fallback : fmaxf(low, fminf(high, v));
}
static double seconds(void) { return (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency(); }
static int check(XrInstance instance, XrResult result, const char *call)
{
    if (XR_SUCCEEDED(result))
        return 1;
    char text[XR_MAX_RESULT_STRING_SIZE] = "";
    if (instance)
        xrResultToString(instance, result, text);
    fprintf(stderr, "[vr] %s: %d %s\n", call, result, text);
    return 0;
}
#define TRY(call)                                                                                            \
    do {                                                                                                     \
        if (!check(instance, (call), #call))                                                                 \
            goto cleanup;                                                                                    \
    } while (0)
static XrPath path(XrInstance instance, const char *s)
{
    XrPath p = 0;
    check(instance, xrStringToPath(instance, s, &p), s);
    return p;
}
enum { STICK, THROTTLE, BRAKE, A, B, X, Y, LB, RB, MENU, VIEW, UP, DOWN, LEFT, RIGHT, ACT_COUNT };
static const char *names[] = {"steer", "throttle",    "brake",        "a",    "b",    "x",
                              "y",     "left_bumper", "right_bumper", "menu", "view", "up",
                              "down",  "left",        "right"};
#define L "/user/hand/left/input/"
#define R "/user/hand/right/input/"
static const char *frame_paths[] = {
    L "thumbstick", R "trigger/value", L "trigger/value",   R "a/click",         R "b/click",
    R "x/click",    R "y/click",       L "bumper/click",    R "bumper/click",    R "menu/click",
    L "view/click", L "dpad_up/click", L "dpad_down/click", L "dpad_left/click", L "dpad_right/click"};
static const char *index_paths[] = {L "thumbstick",
                                    R "trigger/value",
                                    L "trigger/value",
                                    R "a/click",
                                    R "b/click",
                                    R "thumbstick/click",
                                    L "thumbstick/click",
                                    L "squeeze/value",
                                    R "squeeze/value",
                                    L "b/click",
                                    L "a/click",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL};
static const char *touch_paths[] = {L "thumbstick",
                                    R "trigger/value",
                                    L "trigger/value",
                                    R "a/click",
                                    R "b/click",
                                    L "x/click",
                                    R "thumbstick/click",
                                    L "squeeze/value",
                                    R "squeeze/value",
                                    L "menu/click",
                                    L "y/click",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL};
static int suggest(XrInstance instance, XrAction *actions, const char *profile, const char **paths)
{
    XrActionSuggestedBinding bindings[ACT_COUNT];
    uint32_t count = 0;
    for (int i = 0; i < ACT_COUNT; i++)
        if (paths[i])
            bindings[count++] = (XrActionSuggestedBinding){actions[i], path(instance, paths[i])};
    XrInteractionProfileSuggestedBinding b = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    b.interactionProfile = path(instance, profile);
    b.countSuggestedBindings = count;
    b.suggestedBindings = bindings;
    XrResult result = xrSuggestInteractionProfileBindings(instance, &b);
    printf("[vr] binding profile=%s result=%d\n", profile, result);
    return check(instance, result, "xrSuggestInteractionProfileBindings");
}
static vr_input_t sample(XrInstance instance, XrSession session, XrActionSet set, XrAction *actions,
                         int focused, int *view)
{
    vr_input_t out = {0};
    *view = 0;
    out.focused = focused;
    if (!focused)
        return out;
    XrActiveActionSet active = {set, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (!check(instance, xrSyncActions(session, &sync), "xrSyncActions"))
        return out;
    static const int bits[ACT_COUNT] = {-1,     -1,      -1,        GBTN_B,    GBTN_C,
                                        GBTN_A, GBTN_Z,  GBTN_X,    GBTN_Y,    GBTN_START,
                                        -1,     GBTN_UP, GBTN_DOWN, GBTN_LEFT, GBTN_RIGHT};
    for (int i = 0; i < ACT_COUNT; i++) {
        XrActionStateGetInfo get = {XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = actions[i];
        if (i == STICK) {
            XrActionStateVector2f v = {XR_TYPE_ACTION_STATE_VECTOR2F};
            xrGetActionStateVector2f(session, &get, &v);
            if (v.isActive) {
                out.steer = v.currentState.x;
                if (v.currentState.y > .55f)
                    out.buttons |= 1 << GBTN_UP;
                if (v.currentState.y < -.55f)
                    out.buttons |= 1 << GBTN_DOWN;
            }
        } else if (i == THROTTLE || i == BRAKE) {
            XrActionStateFloat f = {XR_TYPE_ACTION_STATE_FLOAT};
            xrGetActionStateFloat(session, &get, &f);
            if (f.isActive) {
                if (i == THROTTLE)
                    out.throttle = f.currentState;
                else
                    out.brake = f.currentState;
            }
        } else {
            XrActionStateBoolean b = {XR_TYPE_ACTION_STATE_BOOLEAN};
            xrGetActionStateBoolean(session, &get, &b);
            if (b.isActive && b.currentState) {
                if (i == VIEW)
                    *view = 1;
                else if (bits[i] >= 0)
                    out.buttons |= 1 << bits[i];
            }
        }
    }
    if (out.steer < -.55f)
        out.buttons |= 1 << GBTN_LEFT;
    if (out.steer > .55f)
        out.buttons |= 1 << GBTN_RIGHT;
    return out;
}
typedef struct {
    XrSwapchain handle;
    XrSwapchainImageOpenGLKHR *images;
    uint32_t count;
    int w, h, waited;
    vr_msaa_target_t msaa;
} chain_t;
static vr_msaa_gl_api_t msaa_api_current(void)
{
    const vr_msaa_gl_api_t gl = {
        glGetIntegerv,
        glGenFramebuffers,
        glDeleteFramebuffers,
        glBindFramebuffer,
        glCheckFramebufferStatus,
        glGenRenderbuffers,
        glDeleteRenderbuffers,
        glBindRenderbuffer,
        glRenderbufferStorageMultisample,
        glGetRenderbufferParameteriv,
        glFramebufferRenderbuffer,
        glDrawBuffer,
        glReadBuffer,
        glIsEnabled,
        glEnable,
        glDisable,
        glBlitFramebuffer,
        (void (*)(GLenum, GLsizei, const GLenum *))SDL_GL_GetProcAddress("glInvalidateFramebuffer"),
    };
    return gl;
}
static int make_chain(XrInstance instance, XrSession session, chain_t *c, int w, int h)
{
    c->w = w;
    c->h = h;
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = GL_SRGB8_ALPHA8;
    info.sampleCount = 1;
    info.width = w;
    info.height = h;
    info.faceCount = info.arraySize = info.mipCount = 1;
    if (!check(instance, xrCreateSwapchain(session, &info, &c->handle), "xrCreateSwapchain"))
        return 0;
    if (!check(instance, xrEnumerateSwapchainImages(c->handle, 0, &c->count, NULL),
               "xrEnumerateSwapchainImages") ||
        !c->count || c->count > 64)
        return 0;
    c->images = calloc(c->count, sizeof(*c->images));
    if (!c->images)
        return 0;
    for (uint32_t i = 0; i < c->count; i++)
        c->images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    if (!check(instance,
               xrEnumerateSwapchainImages(c->handle, c->count, &c->count,
                                          (XrSwapchainImageBaseHeader *)c->images),
               "xrEnumerateSwapchainImages"))
        return 0;
    printf("[vr] swapchain %dx%d images=%u srgb samples=1\n", w, h, c->count);
    return 1;
}
static int acquire(XrInstance instance, chain_t *c, GLuint framebuffer)
{
    uint32_t index;
    XrSwapchainImageAcquireInfo a = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!check(instance, xrAcquireSwapchainImage(c->handle, &a, &index), "xrAcquireSwapchainImage"))
        return 0;
    XrSwapchainImageWaitInfo w = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    w.timeout = 100000000; /* Recheck termination between bounded waits. */
    XrResult waited;
    double deadline = seconds() + 2;
    do {
        waited = xrWaitSwapchainImage(c->handle, &w);
    } while (waited == XR_TIMEOUT_EXPIRED && !stopped && seconds() < deadline);
    if (waited == XR_TIMEOUT_EXPIRED || !check(instance, waited, "xrWaitSwapchainImage"))
        return 0;
    c->waited = 1;
    if (index >= c->count)
        return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->images[index].image, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
static int release(XrInstance instance, chain_t *c)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    XrSwapchainImageReleaseInfo r = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    int ok = check(instance, xrReleaseSwapchainImage(c->handle, &r), "xrReleaseSwapchainImage");
    if (ok)
        c->waited = 0;
    return ok;
}
/* Timer results are consumed only when the driver reports them ready. A busy
   ring slot is skipped rather than waited on or overwritten. */
typedef struct {
    PFNGLGENQUERIESPROC gen;
    PFNGLBEGINQUERYPROC begin;
    PFNGLENDQUERYPROC end;
    PFNGLGETQUERYOBJECTIVPROC available;
    PFNGLGETQUERYOBJECTUI64VPROC result;
    GLuint query[4][2];
    int pending[4][2];
    unsigned owner[4][2];
} gpu_timers_t;
static void gpu_timers_init(gpu_timers_t *timers)
{
    timers->gen = (PFNGLGENQUERIESPROC)SDL_GL_GetProcAddress("glGenQueries");
    timers->begin = (PFNGLBEGINQUERYPROC)SDL_GL_GetProcAddress("glBeginQuery");
    timers->end = (PFNGLENDQUERYPROC)SDL_GL_GetProcAddress("glEndQuery");
    timers->available = (PFNGLGETQUERYOBJECTIVPROC)SDL_GL_GetProcAddress("glGetQueryObjectiv");
    timers->result = (PFNGLGETQUERYOBJECTUI64VPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    if (timers->gen && timers->begin && timers->end && timers->available && timers->result)
        timers->gen(8, &timers->query[0][0]);
}
typedef struct {
    double start, wait, prepare, eyes, acquire, release, gpu[2];
    unsigned frames, rendered, submitted, gpu_samples[2];
    long simulation;
} performance_t;

/* Bounded CPU recorder. No file writes, readbacks or waits during frames.
   GPU results retain their issuing frame number, even when read much later. */
enum { TIMING_CAPACITY = 32768 };
typedef struct {
    double start, wait, begin, locate, prepare, acquire[2], timer[2], eye[2], release[2], panel, end, total;
    double gpu[2];
    XrTime predicted, period;
    long simulation;
    int draw, submitted, immersive;
    vr_prepare_timing_t work;
} frame_timing_t;
static void timing_save(const frame_timing_t *rows, unsigned count)
{
    if (!rows) return;
    FILE *f=fopen("vr-timing.csv","w");
    if (!f) { perror("vr-timing.csv"); return; }
    fputs("frame,start_s,predicted_ns,period_ns,simulation,draw,submitted,immersive,ticks,vertices,wait_ms,begin_ms,locate_input_ms,prepare_ms,simulation_ms,audio_ms,frame_upload_ms,mesh_ms,vertex_upload_ms,hud_ms,acquire_left_ms,timer_left_ms,eye_left_ms,release_left_ms,acquire_right_ms,timer_right_ms,eye_right_ms,release_right_ms,panel_ms,end_ms,total_ms,gpu_left_ms,gpu_right_ms\n",f);
    for(unsigned i=0;i<count;i++) {
        const frame_timing_t *r=rows+i;
        fprintf(f,"%u,%.9f,%lld,%lld,%ld,%d,%d,%d,%u,%u",i+1,r->start,
                (long long)r->predicted,(long long)r->period,r->simulation,r->draw,r->submitted,r->immersive,r->work.ticks,r->work.vertices);
        const double values[]={r->wait,r->begin,r->locate,r->prepare,r->work.simulation,r->work.audio,
            r->work.frame_upload,r->work.mesh,r->work.vertex_upload,r->work.hud,
            r->acquire[0],r->timer[0],r->eye[0],r->release[0],r->acquire[1],r->timer[1],r->eye[1],r->release[1],r->panel,r->end,r->total};
        for(unsigned k=0;k<sizeof(values)/sizeof(values[0]);k++)fprintf(f,",%.6f",values[k]*1000);
        fprintf(f,",%.6f,%.6f\n",r->gpu[0],r->gpu[1]);
    }
    fclose(f);
    printf("[vr-perf] timing_file=vr-timing.csv rows=%u capacity=%d\n",count,TIMING_CAPACITY);
}

int vr_xr_run(SDL_Window *window, const vr_callbacks_t *cb)
{
    XrInstance instance = 0;
    XrSession session = 0;
    XrSpace space = 0;
    XrActionSet set = 0;
    XrAction actions[ACT_COUNT] = {0};
    chain_t chains[3] = {{0}};
    vr_msaa_gl_api_t msaa_gl = {0};
    GLuint framebuffer = 0;
    int running = 0, result = 1, quit = 0, exiting = 0, frame_started = 0;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    XrTime predicted = 0;
    frame_timing_t *timing = NULL;
    unsigned timing_count = 0;
    double started = seconds(), exit_at = 0, limit = vr_env("VRDE_VR_AUTO_EXIT", 0, 0, 3600);
    int force = limit > 0 && vr_env("VRDE_VR_FORCE_RENDER", 0, 0, 1);
    sfvr_view_button view_button = {0};
    XrPosef origin = {{0, 0, 0, 1}, {0, 0, 0}}, anchor = origin;
    int recenter = 1, was_immersive = -1;
    signal(SIGTERM, stop_signal);
    signal(SIGINT, stop_signal);
    SDL_GL_SetSwapInterval(0);
    if (!gl_load_functions())
        goto cleanup;
    msaa_gl = msaa_api_current();
    uint32_t count = 0;
    TRY(xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL));
    if (!count || count > 4096)
        goto cleanup;
    XrExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions)
        goto cleanup;
    for (uint32_t i = 0; i < count; i++)
        extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    XrResult er = xrEnumerateInstanceExtensionProperties(NULL, count, &count, extensions);
    int frame_profile = 0;
    for (uint32_t i = 0; i < count && XR_SUCCEEDED(er); i++)
        if (!strcmp(extensions[i].extensionName, "XR_VALVE_frame_controller_interaction"))
            frame_profile = 1;
    free(extensions);
    TRY(er);
    const char *enabled[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, "XR_VALVE_frame_controller_interaction"};
    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(ci.applicationInfo.applicationName, "Virtua Racing VR");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = frame_profile ? 2 : 1;
    ci.enabledExtensionNames = enabled;
    TRY(xrCreateInstance(&ci, &instance));
    XrInstanceProperties properties = {XR_TYPE_INSTANCE_PROPERTIES};
    TRY(xrGetInstanceProperties(instance, &properties));
    printf("[vr] runtime=%s version=%u.%u.%u requested_api=1.0\n", properties.runtimeName,
           XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion),
           XR_VERSION_PATCH(properties.runtimeVersion));
    XrSystemGetInfo si = {XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system;
    TRY(xrGetSystem(instance, &si, &system));
    PFN_xrGetOpenGLGraphicsRequirementsKHR requirements = NULL;
    TRY(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
                              (PFN_xrVoidFunction *)&requirements));
    XrGraphicsRequirementsOpenGLKHR gr = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    TRY(requirements(instance, system, &gr));
    GLint major, minor;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (XR_MAKE_VERSION(major, minor, 0) < gr.minApiVersionSupported ||
        XR_MAKE_VERSION(major, minor, 0) > gr.maxApiVersionSupported)
        goto cleanup;
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_X11)
        goto cleanup;
    Display *display = glXGetCurrentDisplay();
    GLXContext context = glXGetCurrentContext();
    int id = 0, visual = 0, n = 0;
    if (!display || !context || display != wm.info.x11.display ||
        glXQueryContext(display, context, GLX_FBCONFIG_ID, &id) != Success)
        goto cleanup;
    int attributes[] = {GLX_FBCONFIG_ID, id, None};
    GLXFBConfig *configs = glXChooseFBConfig(display, DefaultScreen(display), attributes, &n);
    if (!configs || !n) {
        if (configs)
            XFree(configs);
        goto cleanup;
    }
    GLXFBConfig config = configs[0];
    XFree(configs);
    if (glXGetFBConfigAttrib(display, config, GLX_VISUAL_ID, &visual) != Success)
        goto cleanup;
    XrGraphicsBindingOpenGLXlibKHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
    binding.xDisplay = display;
    binding.visualid = visual;
    binding.glxFBConfig = config;
    binding.glxDrawable = glXGetCurrentDrawable();
    binding.glxContext = context;
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = system;
    TRY(xrCreateSession(instance, &sci, &session));
    XrReferenceSpaceCreateInfo sp = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    sp.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    sp.poseInReferenceSpace.orientation.w = 1;
    TRY(xrCreateReferenceSpace(session, &sp, &space));
    XrActionSetCreateInfo asi = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(asi.actionSetName, "game");
    strcpy(asi.localizedActionSetName, "Game");
    TRY(xrCreateActionSet(instance, &asi, &set));
    for (int i = 0; i < ACT_COUNT; i++) {
        XrActionCreateInfo ac = {XR_TYPE_ACTION_CREATE_INFO};
        strcpy(ac.actionName, names[i]);
        strcpy(ac.localizedActionName, names[i]);
        ac.actionType = i == STICK   ? XR_ACTION_TYPE_VECTOR2F_INPUT
                        : i <= BRAKE ? XR_ACTION_TYPE_FLOAT_INPUT
                                     : XR_ACTION_TYPE_BOOLEAN_INPUT;
        TRY(xrCreateAction(set, &ac, &actions[i]));
    }
    if (frame_profile &&
        !suggest(instance, actions, "/interaction_profiles/valve/frame_controller_valve", frame_paths))
        goto cleanup;
    if (!suggest(instance, actions, "/interaction_profiles/valve/index_controller", index_paths) ||
        !suggest(instance, actions, "/interaction_profiles/oculus/touch_controller", touch_paths))
        goto cleanup;
    XrSessionActionSetsAttachInfo attach = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &set;
    TRY(xrAttachSessionActionSets(session, &attach));
    XrViewConfigurationView config_views[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                               {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    TRY(xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2,
                                          &count, config_views));
    if (count != 2)
        goto cleanup;
    uint32_t format_count = 0;
    int64_t formats[128];
    TRY(xrEnumerateSwapchainFormats(session, 128, &format_count, formats));
    int srgb = 0;
    for (uint32_t i = 0; i < format_count; i++)
        if (formats[i] == GL_SRGB8_ALPHA8)
            srgb = 1;
    if (!srgb)
        goto cleanup;
    for (int i = 0; i < 2; i++) {
        int size = vr_env("VRDE_VR_EYE_SIZE", 2160, 1440, 2160);
        int w = fmin(size, config_views[i].maxImageRectWidth),
            h = fmin(size, config_views[i].maxImageRectHeight);
        if (!make_chain(instance, session, &chains[i], w, h) ||
            !vr_msaa_target_create(&msaa_gl, &chains[i].msaa, w, h))
            goto cleanup;
    }
    if (!make_chain(instance, session, &chains[2], 640, 448))
        goto cleanup;
    glGenFramebuffers(1, &framebuffer);
    unsigned long frames = 0, rendered = 0, standby = 0;
    performance_t perf = {.start = seconds(), .simulation = cb->simulation_frames()};
    gpu_timers_t timers = {0};
    int timing_gpu = vr_env("VRDE_VR_TIMING_GPU", 0, 0, 1);
    if (timing_gpu) gpu_timers_init(&timers);
    if (vr_env("VRDE_VR_TIMING", 0, 0, 1)) {
        timing = calloc(TIMING_CAPACITY,sizeof(*timing));
        if (!timing) goto cleanup;
        /* Touch pages before recording, outside the frame deadline. */
        for(unsigned i=0;i<TIMING_CAPACITY;i++)timing[i].gpu[0]=timing[i].gpu[1]=-1;
    }
    struct timespec wall;
    clock_gettime(CLOCK_REALTIME,&wall);
    printf("[vr-perf] timing=%d timing_gpu=%d realtime_ns=%lld clock_s=%.9f force=%d\n",
           timing!=NULL,timing_gpu,(long long)wall.tv_sec*1000000000+wall.tv_nsec,seconds(),force);
    vr_cpu_init(vr_env("VRDE_VR_CPU_AFFINITY", 1, 0, 1));
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT)
                stopped = 1;
        XrEventDataBuffer e = {XR_TYPE_EVENT_DATA_BUFFER};
        XrResult poll;
        while ((poll = xrPollEvent(instance, &e)) == XR_SUCCESS) {
            if (e.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                state = ((XrEventDataSessionStateChanged *)&e)->state;
                printf("[vr] session_state=%d\n", state);
                if (state == XR_SESSION_STATE_READY && !running && !exiting) {
                    XrSessionBeginInfo b = {XR_TYPE_SESSION_BEGIN_INFO};
                    b.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    TRY(xrBeginSession(session, &b));
                    running = 1;
                }
                if (state == XR_SESSION_STATE_STOPPING && running) {
                    TRY(xrEndSession(session));
                    running = 0;
                }
                if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING ||
                    (state == XR_SESSION_STATE_IDLE && exiting))
                    quit = 1;
            } else if (e.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
                quit = 1;
            else if (e.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
                recenter = 1;
            e.type = XR_TYPE_EVENT_DATA_BUFFER;
        }
        if (XR_FAILED(poll))
            goto cleanup;
        if (quit)
            break;
        if ((stopped || (limit > 0 && seconds() - started >= limit)) && !exiting) {
            if (!running)
                break;
            TRY(xrRequestExitSession(session));
            exiting = 1;
            exit_at = seconds();
        }
        if (exiting) {
            if (seconds() - exit_at > 5)
                break;
            SDL_Delay(5);
            continue;
        }
        if (!running) {
            if (seconds() - started > 30)
                goto cleanup;
            SDL_Delay(10);
            continue;
        }
        XrFrameWaitInfo wi = {XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState fs = {XR_TYPE_FRAME_STATE};
        double stage = seconds();
        frame_timing_t *row = timing && frames<TIMING_CAPACITY ? timing+frames : NULL;
        if (row) row->start=stage;
        TRY(xrWaitFrame(session, &wi, &fs));
        perf.wait += seconds() - stage;
        if (row) { row->wait=seconds()-stage; row->predicted=fs.predictedDisplayTime; row->period=fs.predictedDisplayPeriod; }
        perf.frames++;
        predicted = fs.predictedDisplayTime;
        XrFrameBeginInfo bi = {XR_TYPE_FRAME_BEGIN_INFO};
        stage=seconds();
        TRY(xrBeginFrame(session, &bi));
        if (row) row->begin=seconds()-stage;
        stage=seconds();
        frame_started = 1;
        frames++;
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        XrViewState vs = {XR_TYPE_VIEW_STATE};
        XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        li.displayTime = predicted;
        li.space = space;
        TRY(xrLocateViews(session, &li, &vs, 2, &count, views));
        int valid = count == 2 && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
                    (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
        if (force) {
            /* Explicit finite diagnostics may synthesize tracking in standby. Normal play
               never invents eye separation or orientation. */
            float ipd = .064f;
            if (valid) {
                XrVector3f a = views[0].pose.position, b = views[1].pose.position;
                float measured =
                    sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
                if (measured > .04f && measured < .09f)
                    ipd = measured;
            }
            XrQuaternionf q = vr_yaw(vr_env("VRDE_VR_DIAG_YAW", 0, -180, 180) * M_PI / 180);
            for (int eye = 0; eye < 2; eye++) {
                XrVector3f offset = vr_rotate(q, (XrVector3f){(eye ? 1 : -1) * ipd / 2, 0, 0});
                views[eye].pose = (XrPosef){q, {offset.x, 1.6f, offset.z}};
                if (!valid)
                    views[eye].fov = (XrFovf){-.785398f, .785398f, .785398f, -.785398f};
            }
            valid = 1;
        }
        int draw = (fs.shouldRender || force) && valid;
        int pressed = 0;
        vr_input_t input =
            sample(instance, session, set, actions, state == XR_SESSION_STATE_FOCUSED, &pressed);
        input.timing=row?&row->work:NULL;
        if (!input.focused)
            sfvr_view_reset(&view_button);
        unsigned events = sfvr_view_update(&view_button, NULL, pressed, predicted * 1e-9);
        if (events & SFVR_VIEW_RECENTRE)
            recenter = 1;
        if (events & SFVR_VIEW_SHORT_PRESS)
            input.buttons |= 1 << GBTN_MODE;
        XrPosef centre = views[0].pose;
        for (int axis = 0; axis < 3; axis++)
            ((float *)&centre.position)[axis] =
                (((float *)&views[0].pose.position)[axis] + ((float *)&views[1].pose.position)[axis]) * .5f;
        if (valid && recenter) {
            origin = centre;
            XrVector3f forward = vr_rotate(centre.orientation, (XrVector3f){0, 0, -1});
            origin.orientation = force ? vr_yaw(0) : vr_yaw(atan2f(-forward.x, -forward.z));
            anchor = origin;
            recenter = 0;
        }
        if (row) row->locate=seconds()-stage;
        stage = seconds();
        int immersive = cb->prepare(predicted * 1e-9, fs.predictedDisplayPeriod * 1e-9,
                                    draw && (input.focused || force), &input);
        if (immersive < 0) goto cleanup;
        perf.prepare += seconds() - stage;
        if (row) { row->prepare=seconds()-stage; row->simulation=cb->simulation_frames(); row->draw=draw; row->immersive=immersive; }
        if (immersive != was_immersive) {
            anchor = origin;
            was_immersive = immersive;
        }
        XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                  {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        XrCompositionLayerProjection projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        projection.space = space;
        projection.viewCount = 2;
        projection.views = pv;
        XrCompositionLayerQuad panel = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        panel.space = space;
        panel.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        const XrCompositionLayerBaseHeader *layers[2] = {(const XrCompositionLayerBaseHeader *)&projection,
                                                         (const XrCompositionLayerBaseHeader *)&panel};
        if (draw) {
            for (int eye = 0; eye < 2; eye++) {
                stage = seconds();
                if (!acquire(instance, &chains[eye], framebuffer))
                    goto cleanup;
                perf.acquire += seconds() - stage;
                if (row) row->acquire[eye]=seconds()-stage;
                XrPosef relative = vr_relative_pose(views[eye].pose, origin);
                /* Clamp physical lean; preserve measured eye offset about the head centre. */
                XrPosef head = vr_relative_pose(centre, origin);
                float length = sqrtf(head.position.x * head.position.x + head.position.y * head.position.y +
                                     head.position.z * head.position.z);
                if (length > .35f) {
                    float k = .35f / length - 1;
                    relative.position.x += head.position.x * k;
                    relative.position.y += head.position.y * k;
                    relative.position.z += head.position.z * k;
                }
                int slot = frames % 4;
                if (row) stage=seconds();
                if (timers.query[slot][eye] && timers.pending[slot][eye]) {
                    GLint ready = 0;
                    timers.available(timers.query[slot][eye], GL_QUERY_RESULT_AVAILABLE, &ready);
                    if (ready) {
                        GLuint64 ns = 0;
                        timers.result(timers.query[slot][eye], GL_QUERY_RESULT, &ns);
                        perf.gpu[eye] += ns * 1e-6;
                        perf.gpu_samples[eye]++;
                        unsigned owner=timers.owner[slot][eye];
                        if (timing && owner<TIMING_CAPACITY)timing[owner].gpu[eye]=ns*1e-6;
                        timers.pending[slot][eye] = 0;
                    }
                }
                int measure = timers.query[slot][eye] && !timers.pending[slot][eye];
                if (measure)
                    timers.begin(GL_TIME_ELAPSED, timers.query[slot][eye]);
                if (row) row->timer[eye]=seconds()-stage;
                stage = seconds();
                GLboolean multisample_was_enabled = glIsEnabled(GL_MULTISAMPLE);
                GLboolean srgb_was_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
                glEnable(GL_MULTISAMPLE);
                glBindFramebuffer(GL_FRAMEBUFFER, chains[eye].msaa.framebuffer);
                cb->eye(eye, relative, views[eye].fov, chains[eye].w, chains[eye].h);
                int resolved = vr_msaa_target_resolve(&msaa_gl, &chains[eye].msaa, framebuffer,
                                                       chains[eye].w, chains[eye].h);
                if (measure) {
                    timers.end(GL_TIME_ELAPSED);
                    timers.pending[slot][eye] = 1;
                    timers.owner[slot][eye] = frames-1;
                }
                if (multisample_was_enabled)
                    glEnable(GL_MULTISAMPLE);
                else
                    glDisable(GL_MULTISAMPLE);
                if (srgb_was_enabled)
                    glEnable(GL_FRAMEBUFFER_SRGB);
                else
                    glDisable(GL_FRAMEBUFFER_SRGB);
                if (!resolved)
                    goto cleanup;
                if (cb->resolved_eye)
                    cb->resolved_eye(eye, relative, chains[eye].w, chains[eye].h);
                perf.eyes += seconds() - stage;
                if (row) row->eye[eye]=seconds()-stage;
                stage = seconds();
                if (!release(instance, &chains[eye]))
                    goto cleanup;
                perf.release += seconds() - stage;
                if (row) row->release[eye]=seconds()-stage;
                pv[eye].pose = views[eye].pose;
                pv[eye].fov = views[eye].fov;
                pv[eye].subImage.swapchain = chains[eye].handle;
                pv[eye].subImage.imageRect.extent = (XrExtent2Di){chains[eye].w, chains[eye].h};
            }
            stage=seconds();
            if (!acquire(instance, &chains[2], framebuffer))
                goto cleanup;
            cb->panel(640, 448, immersive);
            if (!release(instance, &chains[2]))
                goto cleanup;
            if (row) row->panel=seconds()-stage;
            panel.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            panel.subImage.swapchain = chains[2].handle;
            panel.subImage.imageRect.extent = (XrExtent2Di){640, 448};
            panel.pose = immersive ? centre : anchor;
            XrVector3f forward = vr_rotate(panel.pose.orientation, (XrVector3f){0, 0, -1});
            float yaw = atan2f(-forward.x, -forward.z);
            panel.pose.orientation = vr_yaw(yaw); /* Explicit no-roll readable panel. */
            float distance = vr_env("VRDE_VR_HUD_DISTANCE", 2, 1, 5);
            XrVector3f offset =
                vr_rotate(panel.pose.orientation, (XrVector3f){0, immersive ? -.18f : 0, -distance});
            panel.pose.position.x += offset.x;
            panel.pose.position.y += offset.y;
            panel.pose.position.z += offset.z;
            panel.size = (XrExtent2Df){1.9f * distance / 2, 1.425f * distance / 2};
            rendered++;
            perf.rendered++;
        } else
            standby++;
        XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
        end.displayTime = predicted;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = draw && fs.shouldRender ? 2 : 0;
        end.layers = end.layerCount ? layers : NULL;
        stage=seconds();
        TRY(xrEndFrame(session, &end));
        if (row) { row->end=seconds()-stage; row->total=seconds()-row->start; row->submitted=end.layerCount!=0; timing_count++; }
        perf.submitted += end.layerCount != 0;
        frame_started = 0;
        if (seconds() - perf.start >= 10) {
            double span = seconds() - perf.start;
            long simulation = cb->simulation_frames();
            double divisor = perf.frames ? perf.frames : 1;
            printf("[vr-perf] seconds=%.3f frames=%u submitted=%u rendered=%u force=%d "
                   "display_hz=%.2f emulation_hz=%.2f wait_ms=%.3f prepare_ms=%.3f "
                   "eye_cpu_ms=%.3f acquire_ms=%.3f release_ms=%.3f "
                   "gpu_left_ms=%.3f gpu_right_ms=%.3f gpu_samples=%u,%u\n",
                   span, perf.frames, perf.submitted, perf.rendered, force, perf.frames / span,
                   (simulation - perf.simulation) / span, perf.wait * 1000 / divisor,
                   perf.prepare * 1000 / divisor, perf.eyes * 1000 / divisor, perf.acquire * 1000 / divisor,
                   perf.release * 1000 / divisor,
                   perf.gpu_samples[0] ? perf.gpu[0] / perf.gpu_samples[0] : -1,
                   perf.gpu_samples[1] ? perf.gpu[1] / perf.gpu_samples[1] : -1, perf.gpu_samples[0],
                   perf.gpu_samples[1]);
            perf = (performance_t){.start = seconds(), .simulation = simulation};
        }
    }
    printf("[vr] stopped frames=%lu rendered=%lu standby=%lu\n", frames, rendered, standby);
    result = 0;
cleanup:
    vr_cpu_shutdown();
    timing_save(timing,timing_count);
    free(timing);
    for (int i = 0; i < 3; i++)
        if (chains[i].waited)
            release(instance, &chains[i]);
    if (frame_started && session) {
        XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
        end.displayTime = predicted;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        xrEndFrame(session, &end);
    }
    if (running && session) {
        xrRequestExitSession(session);
        double deadline = seconds() + 2;
        while (running && seconds() < deadline) {
            XrEventDataBuffer e = {XR_TYPE_EVENT_DATA_BUFFER};
            while (xrPollEvent(instance, &e) == XR_SUCCESS) {
                if (e.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED &&
                    ((XrEventDataSessionStateChanged *)&e)->state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    running = 0;
                }
                e.type = XR_TYPE_EVENT_DATA_BUFFER;
            }
            SDL_Delay(5);
        }
    }
    for (int i = 0; i < 3; i++) {
        vr_msaa_target_destroy(&msaa_gl, &chains[i].msaa);
        if (chains[i].handle)
            xrDestroySwapchain(chains[i].handle);
        free(chains[i].images);
    }
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    if (space)
        xrDestroySpace(space);
    if (session)
        xrDestroySession(session);
    if (set)
        xrDestroyActionSet(set);
    if (instance)
        xrDestroyInstance(instance);
    return result;
}
