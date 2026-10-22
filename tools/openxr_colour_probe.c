/*
 * Standalone OpenXR GLX colour probe for Linux ARM64 Steam Frame.
 *
 * Adapted from Madison's G-Diffuser OpenXR probe:
 * https://github.com/Zorkats/G-Diffuser/blob/6520fdb5beeeabf8c53479f276cd8770c6605fde/tools/vr_probe/gdx_openxr_probe.c
 * The adapted portions retain G-Diffuser's MIT license in licenses/G-Diffuser-MIT.txt.
 *
 * This is a runtime graphics-binding and swapchain probe, not a game renderer.
 */
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

typedef void (*PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (*PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (*PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFN_glCheckFramebufferStatus)(GLenum);
typedef void (*PFN_glDeleteFramebuffers)(GLsizei, const GLuint *);

#define PROBE_MAX_ENUM_ITEMS 4096u
#define PROBE_MAX_SWAPCHAIN_IMAGES 64u

static XrInstance instance = XR_NULL_HANDLE;

static double monotonic_seconds(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0.0;
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static const char *session_state_name(XrSessionState state)
{
	switch (state) {
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	default: return "UNKNOWN";
	}
}

static const char *gl_format_name(int64_t format)
{
	if (format == GL_SRGB8_ALPHA8) return "GL_SRGB8_ALPHA8";
	if (format == 0x8058) return "GL_RGBA8";
	if (format == 0x881A) return "GL_RGBA16F";
	return "other";
}

static void report_xr_failure(const char *call, XrResult result, int line)
{
	char text[XR_MAX_RESULT_STRING_SIZE] = "";
	if (instance != XR_NULL_HANDLE)
		xrResultToString(instance, result, text);
	fprintf(stderr, "[openxr-probe] FAIL %s -> %d (%s) at line %d\n", call, result,
		text[0] ? text : "no result string", line);
}

static XrResult end_empty_frame(XrSession session, XrTime display_time)
{
	XrFrameEndInfo info = { XR_TYPE_FRAME_END_INFO };
	info.displayTime = display_time;
	info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	return xrEndFrame(session, &info);
}

#define XR_TRY(call) do { \
	result = (call); \
	if (XR_FAILED(result)) { \
		report_xr_failure(#call, result, __LINE__); \
		goto cleanup; \
	} \
} while (0)

#define XR_FRAME_TRY(call, display_time) do { \
	result = (call); \
	if (XR_FAILED(result)) { \
		report_xr_failure(#call, result, __LINE__); \
		XrResult end_result_ = end_empty_frame(session, display_time); \
		if (XR_FAILED(end_result_)) \
			report_xr_failure("xrEndFrame(after frame error)", end_result_, __LINE__); \
		goto cleanup; \
	} \
} while (0)

static int parse_duration(int argc, char **argv, double *seconds)
{
	char *end = NULL;
	double parsed;
	if (argc == 1) {
		*seconds = 8.0;
		return 1;
	}
	if (argc != 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
		return 0;
	errno = 0;
	parsed = strtod(argv[1], &end);
	if (errno || end == argv[1] || *end || !isfinite(parsed) || parsed < 1.0 || parsed > 120.0)
		return 0;
	*seconds = parsed;
	return 1;
}

static int instance_extension_available(const char *name)
{
	XrResult result;
	uint32_t count = 0;
	XrExtensionProperties *properties = NULL;
	int found = 0;

	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL);
	if (XR_FAILED(result)) {
		report_xr_failure("xrEnumerateInstanceExtensionProperties(count)", result, __LINE__);
		return -1;
	}
	if (count == 0 || count > PROBE_MAX_ENUM_ITEMS)
		return 0;
	properties = calloc(count, sizeof(*properties));
	if (!properties) {
		fprintf(stderr, "[openxr-probe] could not allocate extension list\n");
		return -1;
	}
	for (uint32_t i = 0; i < count; i++)
		properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	result = xrEnumerateInstanceExtensionProperties(NULL, count, &count, properties);
	if (XR_FAILED(result)) {
		report_xr_failure("xrEnumerateInstanceExtensionProperties(list)", result, __LINE__);
		free(properties);
		return -1;
	}
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(properties[i].extensionName, name) == 0)
			found = 1;
	}
	free(properties);
	return found;
}

static int contains_view_config(const XrViewConfigurationType *types, uint32_t count,
				XrViewConfigurationType wanted)
{
	for (uint32_t i = 0; i < count; i++) {
		if (types[i] == wanted)
			return 1;
	}
	return 0;
}

static int contains_blend_mode(const XrEnvironmentBlendMode *modes, uint32_t count,
				XrEnvironmentBlendMode wanted)
{
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i] == wanted)
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int exit_code = 1;
	int sdl_initialized = 0;
	int session_running = 0;
	int exit_requested = 0;
	int quit = 0;
	int begin_seen = 0;
	XrResult result = XR_SUCCESS;
	double requested_seconds = 8.0;
	double app_start = 0.0;
	double not_ready_start = 0.0;
	double exit_request_time = 0.0;

	SDL_Window *window = NULL;
	SDL_GLContext gl_context = NULL;
	XrSession session = XR_NULL_HANDLE;
	XrSpace local_space = XR_NULL_HANDLE;
	XrSwapchain swapchains[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
	XrSwapchainImageOpenGLKHR *swapchain_images[2] = { NULL, NULL };
	uint32_t swapchain_image_counts[2] = { 0, 0 };
	uint32_t eye_width[2] = { 0, 0 };
	uint32_t eye_height[2] = { 0, 0 };
	GLXFBConfig framebuffer_config = NULL;
	GLuint framebuffer = 0;
	PFN_glGenFramebuffers gl_gen_framebuffers = NULL;
	PFN_glBindFramebuffer gl_bind_framebuffer = NULL;
	PFN_glFramebufferTexture2D gl_framebuffer_texture_2d = NULL;
	PFN_glCheckFramebufferStatus gl_check_framebuffer_status = NULL;
	PFN_glDeleteFramebuffers gl_delete_framebuffers = NULL;
	XrSessionState state = XR_SESSION_STATE_UNKNOWN;
	uint64_t frames_waited = 0;
	uint64_t frames_should_render = 0;
	uint64_t frames_with_valid_pose = 0;
	uint64_t frames_with_projection_layer = 0;
	uint64_t frames_zero_layers = 0;
	uint64_t standby_zero_layer_frames = 0;
	uint64_t frame_cpu_samples = 0;
	XrTime first_predicted_time = 0;
	XrTime last_predicted_time = 0;
	XrTime last_predicted_period = 0;
	double frame_cpu_ms_sum = 0.0;
	XrViewConfigurationView *view_configuration_views = NULL;
	uint32_t view_count = 0;
	int64_t *formats = NULL;
	uint32_t format_count = 0;
	int64_t selected_format = 0;
	XrViewConfigurationType *view_types = NULL;
	uint32_t view_type_count = 0;
	XrEnvironmentBlendMode *blend_modes = NULL;
	uint32_t blend_mode_count = 0;
	XrReferenceSpaceType *reference_spaces = NULL;
	uint32_t reference_space_count = 0;
	Display *x_display = NULL;
	GLXContext current_glx_context = NULL;
	GLXDrawable current_drawable = 0;
	XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
	XrCompositionLayerProjectionView projection_views[2];
	XrCompositionLayerProjection projection_layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	const XrCompositionLayerBaseHeader *layers[1];

	if (!parse_duration(argc, argv, &requested_seconds)) {
		fprintf(stderr, "Usage: %s [SECONDS] (1 to 120, default 8)\n", argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "[openxr-probe] SDL_Init failed: %s\n", SDL_GetError());
		goto cleanup;
	}
	sdl_initialized = 1;
	if (SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0) != 0 ||
	    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0 ||
	    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0 ||
	    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) != 0 ||
	    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 0) {
		fprintf(stderr, "[openxr-probe] SDL_GL_SetAttribute failed: %s\n", SDL_GetError());
		goto cleanup;
	}
	window = SDL_CreateWindow("Virtua Racing OpenXR colour probe", SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED, 320, 240,
				  SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	if (!window) {
		fprintf(stderr, "[openxr-probe] SDL_CreateWindow failed: %s\n", SDL_GetError());
		goto cleanup;
	}
	gl_context = SDL_GL_CreateContext(window);
	if (!gl_context) {
		fprintf(stderr, "[openxr-probe] SDL_GL_CreateContext (OpenGL 4.3 core) failed: %s\n", SDL_GetError());
		goto cleanup;
	}
	if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
		fprintf(stderr, "[openxr-probe] SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
		goto cleanup;
	}

	SDL_SysWMinfo wm_info;
	SDL_VERSION(&wm_info.version);
	if (SDL_GetWindowWMInfo(window, &wm_info) != SDL_TRUE || wm_info.subsystem != SDL_SYSWM_X11) {
		fprintf(stderr, "[openxr-probe] SDL must create an X11 window for OpenXR GLX binding (driver=%s)\n",
			SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown");
		goto cleanup;
	}
	x_display = glXGetCurrentDisplay();
	current_glx_context = glXGetCurrentContext();
	current_drawable = glXGetCurrentDrawable();
	if (!x_display || !current_glx_context || current_drawable == 0 || x_display != wm_info.info.x11.display) {
		fprintf(stderr, "[openxr-probe] current SDL context is not a usable GLX context\n");
		goto cleanup;
	}

	GLint gl_major = 0, gl_minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
	glGetIntegerv(GL_MINOR_VERSION, &gl_minor);
	const char *gl_vendor = (const char *)glGetString(GL_VENDOR);
	const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
	const char *gl_version = (const char *)glGetString(GL_VERSION);
	printf("[sdl] video_driver=%s wm_subsystem=x11\n", SDL_GetCurrentVideoDriver());
	printf("[gl] vendor=%s\n[gl] renderer=%s\n[gl] version=%s\n",
		gl_vendor ? gl_vendor : "unknown", gl_renderer ? gl_renderer : "unknown",
		gl_version ? gl_version : "unknown");
	if (gl_major < 4 || (gl_major == 4 && gl_minor < 3)) {
		fprintf(stderr, "[openxr-probe] OpenGL %d.%d is below the required 4.3 context\n", gl_major, gl_minor);
		goto cleanup;
	}

	int fb_config_id = 0;
	int visual_id = 0;
	if (glXQueryContext(x_display, current_glx_context, GLX_FBCONFIG_ID, &fb_config_id) != Success ||
	    fb_config_id == 0) {
		fprintf(stderr, "[openxr-probe] glXQueryContext did not return a framebuffer config\n");
		goto cleanup;
	}
	int config_attributes[] = { GLX_FBCONFIG_ID, fb_config_id, None };
	int config_count = 0;
	GLXFBConfig *configs = glXChooseFBConfig(x_display, DefaultScreen(x_display), config_attributes, &config_count);
	if (!configs || config_count < 1) {
		if (configs)
			XFree(configs);
		fprintf(stderr, "[openxr-probe] could not recover the active GLX framebuffer config\n");
		goto cleanup;
	}
	framebuffer_config = configs[0];
	XFree(configs);
	if (glXGetFBConfigAttrib(x_display, framebuffer_config, GLX_VISUAL_ID, &visual_id) != Success || visual_id == 0) {
		fprintf(stderr, "[openxr-probe] active GLX config has no X11 visual\n");
		goto cleanup;
	}

	int extension_state = instance_extension_available(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
	if (extension_state <= 0) {
		if (extension_state == 0)
			fprintf(stderr, "[openxr-probe] runtime does not advertise %s\n", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
		goto cleanup;
	}
	const char *extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo instance_info = { XR_TYPE_INSTANCE_CREATE_INFO };
	snprintf(instance_info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE,
		 "Virtua Racing OpenXR colour probe");
	instance_info.applicationInfo.applicationVersion = 1;
	snprintf(instance_info.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Steam Frame probe");
	instance_info.applicationInfo.engineVersion = 1;
	instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	instance_info.enabledExtensionCount = 1;
	instance_info.enabledExtensionNames = extensions;
	XR_TRY(xrCreateInstance(&instance_info, &instance));

	XrInstanceProperties instance_properties = { XR_TYPE_INSTANCE_PROPERTIES };
	XR_TRY(xrGetInstanceProperties(instance, &instance_properties));
	printf("[openxr] runtime=%s version=%u.%u.%u\n", instance_properties.runtimeName,
		XR_VERSION_MAJOR(instance_properties.runtimeVersion), XR_VERSION_MINOR(instance_properties.runtimeVersion),
		XR_VERSION_PATCH(instance_properties.runtimeVersion));

	XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId system_id = XR_NULL_SYSTEM_ID;
	XR_TRY(xrGetSystem(instance, &system_info, &system_id));
	XrSystemProperties system_properties = { XR_TYPE_SYSTEM_PROPERTIES };
	XR_TRY(xrGetSystemProperties(instance, system_id, &system_properties));
	printf("[openxr] system=%s vendor=%u max_swapchain=%ux%u max_layers=%u orientation_tracking=%u position_tracking=%u\n",
		system_properties.systemName, system_properties.vendorId,
		system_properties.graphicsProperties.maxSwapchainImageWidth,
		system_properties.graphicsProperties.maxSwapchainImageHeight,
		system_properties.graphicsProperties.maxLayerCount,
		system_properties.trackingProperties.orientationTracking,
		system_properties.trackingProperties.positionTracking);

	XR_TRY(xrEnumerateViewConfigurations(instance, system_id, 0, &view_type_count, NULL));
	if (view_type_count == 0 || view_type_count > PROBE_MAX_ENUM_ITEMS) {
		fprintf(stderr, "[openxr-probe] runtime returned an invalid view-configuration count: %u\n", view_type_count);
		goto cleanup;
	}
	view_types = calloc(view_type_count, sizeof(*view_types));
	if (!view_types) {
		fprintf(stderr, "[openxr-probe] could not allocate view-configuration list\n");
		goto cleanup;
	}
	XR_TRY(xrEnumerateViewConfigurations(instance, system_id, view_type_count, &view_type_count, view_types));
	printf("[openxr] stereo_view_config=%s\n",
		contains_view_config(view_types, view_type_count, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) ? "available" : "missing");
	if (!contains_view_config(view_types, view_type_count, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO))
		goto cleanup;
	free(view_types);
	view_types = NULL;

	XR_TRY(xrEnumerateViewConfigurationViews(instance, system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, NULL));
	if (view_count != 2) {
		fprintf(stderr, "[openxr-probe] expected two primary stereo views, runtime returned %u\n", view_count);
		goto cleanup;
	}
	view_configuration_views = calloc(view_count, sizeof(*view_configuration_views));
	if (!view_configuration_views) {
		fprintf(stderr, "[openxr-probe] could not allocate stereo view configuration\n");
		goto cleanup;
	}
	for (uint32_t i = 0; i < view_count; i++)
		view_configuration_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	XR_TRY(xrEnumerateViewConfigurationViews(instance, system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count, &view_count,
		view_configuration_views));
	for (uint32_t i = 0; i < view_count; i++) {
		eye_width[i] = view_configuration_views[i].recommendedImageRectWidth;
		eye_height[i] = view_configuration_views[i].recommendedImageRectHeight;
		printf("[openxr] eye%u recommended=%ux%u max=%ux%u samples=%u/%u\n", i,
			eye_width[i], eye_height[i], view_configuration_views[i].maxImageRectWidth,
			view_configuration_views[i].maxImageRectHeight,
			view_configuration_views[i].recommendedSwapchainSampleCount,
			view_configuration_views[i].maxSwapchainSampleCount);
		if (!eye_width[i] || !eye_height[i] || eye_width[i] > system_properties.graphicsProperties.maxSwapchainImageWidth ||
		    eye_height[i] > system_properties.graphicsProperties.maxSwapchainImageHeight) {
			fprintf(stderr, "[openxr-probe] runtime returned invalid dimensions for eye%u\n", i);
			goto cleanup;
		}
	}

	XR_TRY(xrEnumerateEnvironmentBlendModes(instance, system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &blend_mode_count, NULL));
	if (blend_mode_count == 0 || blend_mode_count > PROBE_MAX_ENUM_ITEMS) {
		fprintf(stderr, "[openxr-probe] invalid environment blend-mode count: %u\n", blend_mode_count);
		goto cleanup;
	}
	blend_modes = calloc(blend_mode_count, sizeof(*blend_modes));
	if (!blend_modes) {
		fprintf(stderr, "[openxr-probe] could not allocate blend-mode list\n");
		goto cleanup;
	}
	XR_TRY(xrEnumerateEnvironmentBlendModes(instance, system_id,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, blend_mode_count, &blend_mode_count, blend_modes));
	if (!contains_blend_mode(blend_modes, blend_mode_count, XR_ENVIRONMENT_BLEND_MODE_OPAQUE)) {
		fprintf(stderr, "[openxr-probe] runtime does not support opaque environment blending\n");
		goto cleanup;
	}
	free(blend_modes);
	blend_modes = NULL;

	PFN_xrGetOpenGLGraphicsRequirementsKHR get_gl_requirements = NULL;
	XR_TRY(xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR",
		(PFN_xrVoidFunction *)&get_gl_requirements));
	if (!get_gl_requirements) {
		fprintf(stderr, "[openxr-probe] OpenXR GL requirements function is unavailable\n");
		goto cleanup;
	}
	XrGraphicsRequirementsOpenGLKHR gl_requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	XR_TRY(get_gl_requirements(instance, system_id, &gl_requirements));
	printf("[openxr] gl_requirements min=%u.%u max=%u.%u context=%d.%d required_minimum=4.3\n",
		XR_VERSION_MAJOR(gl_requirements.minApiVersionSupported),
		XR_VERSION_MINOR(gl_requirements.minApiVersionSupported),
		XR_VERSION_MAJOR(gl_requirements.maxApiVersionSupported),
		XR_VERSION_MINOR(gl_requirements.maxApiVersionSupported), gl_major, gl_minor);
	XrVersion context_version = XR_MAKE_VERSION(gl_major, gl_minor, 0);
	if (context_version < gl_requirements.minApiVersionSupported ||
	    context_version > gl_requirements.maxApiVersionSupported) {
		fprintf(stderr, "[openxr-probe] OpenGL context is outside the runtime's supported version range\n");
		goto cleanup;
	}

	XrGraphicsBindingOpenGLXlibKHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
	binding.xDisplay = x_display;
	binding.visualid = (uint32_t)visual_id;
	binding.glxFBConfig = framebuffer_config;
	binding.glxDrawable = current_drawable;
	binding.glxContext = current_glx_context;
	XrSessionCreateInfo session_info = { XR_TYPE_SESSION_CREATE_INFO };
	session_info.next = &binding;
	session_info.systemId = system_id;
	XR_TRY(xrCreateSession(instance, &session_info, &session));
	printf("[openxr] graphics_binding=opengl_xlib session=created\n");

	XR_TRY(xrEnumerateSwapchainFormats(session, 0, &format_count, NULL));
	if (format_count == 0 || format_count > PROBE_MAX_ENUM_ITEMS) {
		fprintf(stderr, "[openxr-probe] invalid swapchain-format count: %u\n", format_count);
		goto cleanup;
	}
	formats = calloc(format_count, sizeof(*formats));
	if (!formats) {
		fprintf(stderr, "[openxr-probe] could not allocate swapchain-format list\n");
		goto cleanup;
	}
	XR_TRY(xrEnumerateSwapchainFormats(session, format_count, &format_count, formats));
	printf("[openxr] swapchain_formats=");
	for (uint32_t i = 0; i < format_count; i++) {
		printf("%s0x%llx(%s)", i ? "," : "", (unsigned long long)formats[i], gl_format_name(formats[i]));
		if (formats[i] == GL_SRGB8_ALPHA8)
			selected_format = formats[i];
	}
	printf("\n");
	if (selected_format == 0) {
		fprintf(stderr, "[openxr-probe] runtime has no GL_SRGB8_ALPHA8 swapchain format\n");
		goto cleanup;
	}
	printf("[openxr] swapchain_format=%s\n", gl_format_name(selected_format));
	free(formats);
	formats = NULL;

	XR_TRY(xrEnumerateReferenceSpaces(session, 0, &reference_space_count, NULL));
	if (reference_space_count == 0 || reference_space_count > PROBE_MAX_ENUM_ITEMS) {
		fprintf(stderr, "[openxr-probe] invalid reference-space count: %u\n", reference_space_count);
		goto cleanup;
	}
	reference_spaces = calloc(reference_space_count, sizeof(*reference_spaces));
	if (!reference_spaces) {
		fprintf(stderr, "[openxr-probe] could not allocate reference-space list\n");
		goto cleanup;
	}
	XR_TRY(xrEnumerateReferenceSpaces(session, reference_space_count, &reference_space_count, reference_spaces));
	int local_space_supported = 0;
	printf("[openxr] reference_spaces=");
	for (uint32_t i = 0; i < reference_space_count; i++) {
		printf("%s%d", i ? "," : "", (int)reference_spaces[i]);
		if (reference_spaces[i] == XR_REFERENCE_SPACE_TYPE_LOCAL)
			local_space_supported = 1;
	}
	printf(" (1=VIEW 2=LOCAL 3=STAGE)\n");
	free(reference_spaces);
	reference_spaces = NULL;
	if (!local_space_supported) {
		fprintf(stderr, "[openxr-probe] runtime does not support LOCAL reference space\n");
		goto cleanup;
	}
	XrReferenceSpaceCreateInfo space_info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	space_info.poseInReferenceSpace.orientation.w = 1.0f;
	XR_TRY(xrCreateReferenceSpace(session, &space_info, &local_space));

	for (uint32_t eye = 0; eye < 2; eye++) {
		XrSwapchainCreateInfo swapchain_info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
		swapchain_info.format = selected_format;
		swapchain_info.sampleCount = 1;
		swapchain_info.width = eye_width[eye];
		swapchain_info.height = eye_height[eye];
		swapchain_info.faceCount = 1;
		swapchain_info.arraySize = 1;
		swapchain_info.mipCount = 1;
		XR_TRY(xrCreateSwapchain(session, &swapchain_info, &swapchains[eye]));
		XR_TRY(xrEnumerateSwapchainImages(swapchains[eye], 0, &swapchain_image_counts[eye], NULL));
		if (swapchain_image_counts[eye] == 0 || swapchain_image_counts[eye] > PROBE_MAX_SWAPCHAIN_IMAGES) {
			fprintf(stderr, "[openxr-probe] invalid swapchain-image count for eye%u: %u\n",
				eye, swapchain_image_counts[eye]);
			goto cleanup;
		}
		swapchain_images[eye] = calloc(swapchain_image_counts[eye], sizeof(*swapchain_images[eye]));
		if (!swapchain_images[eye]) {
			fprintf(stderr, "[openxr-probe] could not allocate swapchain images for eye%u\n", eye);
			goto cleanup;
		}
		for (uint32_t i = 0; i < swapchain_image_counts[eye]; i++)
			swapchain_images[eye][i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
		XR_TRY(xrEnumerateSwapchainImages(swapchains[eye], swapchain_image_counts[eye],
			&swapchain_image_counts[eye], (XrSwapchainImageBaseHeader *)swapchain_images[eye]));
		printf("[openxr] eye%u swapchain=%ux%u images=%u\n", eye, eye_width[eye], eye_height[eye],
			swapchain_image_counts[eye]);
	}

	gl_gen_framebuffers = (PFN_glGenFramebuffers)SDL_GL_GetProcAddress("glGenFramebuffers");
	gl_bind_framebuffer = (PFN_glBindFramebuffer)SDL_GL_GetProcAddress("glBindFramebuffer");
	gl_framebuffer_texture_2d = (PFN_glFramebufferTexture2D)SDL_GL_GetProcAddress("glFramebufferTexture2D");
	gl_check_framebuffer_status = (PFN_glCheckFramebufferStatus)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
	gl_delete_framebuffers = (PFN_glDeleteFramebuffers)SDL_GL_GetProcAddress("glDeleteFramebuffers");
	if (!gl_gen_framebuffers || !gl_bind_framebuffer || !gl_framebuffer_texture_2d ||
	    !gl_check_framebuffer_status || !gl_delete_framebuffers) {
		fprintf(stderr, "[openxr-probe] OpenGL framebuffer entry points are unavailable\n");
		goto cleanup;
	}
	gl_gen_framebuffers(1, &framebuffer);
	if (framebuffer == 0) {
		fprintf(stderr, "[openxr-probe] glGenFramebuffers returned zero\n");
		goto cleanup;
	}
	glEnable(GL_FRAMEBUFFER_SRGB);
	printf("[openxr] colour_space=GL_SRGB8_ALPHA8 framebuffer_srgb=enabled\n");

	while (!quit) {
		SDL_Event sdl_event;
		while (SDL_PollEvent(&sdl_event)) {
			if (sdl_event.type == SDL_QUIT)
				quit = 1;
		}
		if (quit && session_running && !exit_requested) {
			XR_TRY(xrRequestExitSession(session));
			exit_requested = 1;
			exit_request_time = monotonic_seconds();
		}

		XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };
		while ((result = xrPollEvent(instance, &event_buffer)) == XR_SUCCESS) {
			if (event_buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
				const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event_buffer;
				state = changed->state;
				printf("[state] %s\n", session_state_name(state));
				if (state == XR_SESSION_STATE_READY && !session_running && !exit_requested) {
					XrSessionBeginInfo begin_info = { XR_TYPE_SESSION_BEGIN_INFO };
					begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					XR_TRY(xrBeginSession(session, &begin_info));
					session_running = 1;
					begin_seen = 1;
					if (app_start == 0.0)
						app_start = monotonic_seconds();
				}
				if (state == XR_SESSION_STATE_STOPPING && session_running) {
					XR_TRY(xrEndSession(session));
					session_running = 0;
				}
				if (state == XR_SESSION_STATE_IDLE && exit_requested)
					quit = 1;
				if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING)
					quit = 1;
			} else if (event_buffer.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
				printf("[state] INSTANCE_LOSS_PENDING\n");
				quit = 1;
			}
			event_buffer.type = XR_TYPE_EVENT_DATA_BUFFER;
		}
		if (XR_FAILED(result)) {
			report_xr_failure("xrPollEvent", result, __LINE__);
			goto cleanup;
		}

		if (quit)
			break;
		if (exit_requested && monotonic_seconds() - exit_request_time > 15.0) {
			fprintf(stderr, "[openxr-probe] runtime did not stop the session within 15 seconds\n");
			goto cleanup;
		}
		if (!session_running) {
			if (!begin_seen) {
				if (not_ready_start == 0.0)
					not_ready_start = monotonic_seconds();
				if (monotonic_seconds() - not_ready_start > 30.0) {
					fprintf(stderr, "[openxr-probe] session did not become READY within 30 seconds\n");
					goto cleanup;
				}
			}
			SDL_Delay(10);
			continue;
		}
		if (!exit_requested && monotonic_seconds() - app_start >= requested_seconds) {
			XR_TRY(xrRequestExitSession(session));
			exit_requested = 1;
			exit_request_time = monotonic_seconds();
			continue;
		}
		if (exit_requested) {
			SDL_Delay(10);
			continue;
		}

		XrFrameWaitInfo wait_info = { XR_TYPE_FRAME_WAIT_INFO };
		XrFrameState frame_state = { XR_TYPE_FRAME_STATE };
		XR_TRY(xrWaitFrame(session, &wait_info, &frame_state));
		double frame_start = monotonic_seconds();
		frames_waited++;
		if (!first_predicted_time)
			first_predicted_time = frame_state.predictedDisplayTime;
		last_predicted_time = frame_state.predictedDisplayTime;
		last_predicted_period = frame_state.predictedDisplayPeriod;

		XrFrameBeginInfo begin_frame_info = { XR_TYPE_FRAME_BEGIN_INFO };
		XR_TRY(xrBeginFrame(session, &begin_frame_info));
		uint32_t layer_count = 0;
		if (frame_state.shouldRender) {
			frames_should_render++;
			XrViewLocateInfo locate_info = { XR_TYPE_VIEW_LOCATE_INFO };
			locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			locate_info.displayTime = frame_state.predictedDisplayTime;
			locate_info.space = local_space;
			XrViewState view_state = { XR_TYPE_VIEW_STATE };
			uint32_t located_count = 0;
			views[0] = (XrView){ XR_TYPE_VIEW };
			views[1] = (XrView){ XR_TYPE_VIEW };
			XR_FRAME_TRY(xrLocateViews(session, &locate_info, &view_state, 2, &located_count, views),
				frame_state.predictedDisplayTime);
			int valid_pose = located_count == 2 &&
				(view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
				(view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
			if (valid_pose)
				frames_with_valid_pose++;
			if (valid_pose) {
				for (uint32_t eye = 0; eye < 2; eye++) {
					uint32_t image_index = 0;
					XrSwapchainImageAcquireInfo acquire_info = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
					XR_FRAME_TRY(xrAcquireSwapchainImage(swapchains[eye], &acquire_info, &image_index),
						frame_state.predictedDisplayTime);
					XrSwapchainImageWaitInfo image_wait_info = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
					image_wait_info.timeout = XR_INFINITE_DURATION;
					result = xrWaitSwapchainImage(swapchains[eye], &image_wait_info);
					if (XR_FAILED(result)) {
						report_xr_failure("xrWaitSwapchainImage", result, __LINE__);
						XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
						XrResult release_result = xrReleaseSwapchainImage(swapchains[eye], &release_info);
						if (XR_FAILED(release_result))
							report_xr_failure("xrReleaseSwapchainImage(after wait error)", release_result, __LINE__);
						XrResult end_result = end_empty_frame(session, frame_state.predictedDisplayTime);
						if (XR_FAILED(end_result))
							report_xr_failure("xrEndFrame(after swapchain wait error)", end_result, __LINE__);
						goto cleanup;
					}
					if (image_index >= swapchain_image_counts[eye]) {
						fprintf(stderr, "[openxr-probe] runtime returned out-of-range eye%u image index %u\n", eye, image_index);
						XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
						XrResult release_result = xrReleaseSwapchainImage(swapchains[eye], &release_info);
						if (XR_FAILED(release_result))
							report_xr_failure("xrReleaseSwapchainImage(after invalid index)", release_result, __LINE__);
						XrResult end_result = end_empty_frame(session, frame_state.predictedDisplayTime);
						if (XR_FAILED(end_result))
							report_xr_failure("xrEndFrame(after invalid image index)", end_result, __LINE__);
						goto cleanup;
					}
					gl_bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
					gl_framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
						swapchain_images[eye][image_index].image, 0);
					if (gl_check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
						fprintf(stderr, "[openxr-probe] eye%u framebuffer is incomplete\n", eye);
						gl_bind_framebuffer(GL_FRAMEBUFFER, 0);
						XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
						result = xrReleaseSwapchainImage(swapchains[eye], &release_info);
						if (XR_FAILED(result))
							report_xr_failure("xrReleaseSwapchainImage", result, __LINE__);
						XrResult end_result = end_empty_frame(session, frame_state.predictedDisplayTime);
						if (XR_FAILED(end_result))
							report_xr_failure("xrEndFrame(after framebuffer error)", end_result, __LINE__);
						goto cleanup;
					}
					glViewport(0, 0, (GLsizei)eye_width[eye], (GLsizei)eye_height[eye]);
					if (eye == 0)
						glClearColor(0.10f, 0.015f, 0.015f, 1.0f);
					else
						glClearColor(0.015f, 0.025f, 0.10f, 1.0f);
					glClear(GL_COLOR_BUFFER_BIT);
					GLenum gl_error = glGetError();
					gl_bind_framebuffer(GL_FRAMEBUFFER, 0);
					XrSwapchainImageReleaseInfo release_info = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
					XR_FRAME_TRY(xrReleaseSwapchainImage(swapchains[eye], &release_info),
						frame_state.predictedDisplayTime);
					if (gl_error != GL_NO_ERROR) {
						fprintf(stderr, "[openxr-probe] OpenGL error 0x%x while clearing eye%u\n", gl_error, eye);
						XrResult end_result = end_empty_frame(session, frame_state.predictedDisplayTime);
						if (XR_FAILED(end_result))
							report_xr_failure("xrEndFrame(after OpenGL error)", end_result, __LINE__);
						goto cleanup;
					}
					projection_views[eye] = (XrCompositionLayerProjectionView){ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
					projection_views[eye].pose = views[eye].pose;
					projection_views[eye].fov = views[eye].fov;
					projection_views[eye].subImage.swapchain = swapchains[eye];
					projection_views[eye].subImage.imageRect.offset.x = 0;
					projection_views[eye].subImage.imageRect.offset.y = 0;
					projection_views[eye].subImage.imageRect.extent.width = (int32_t)eye_width[eye];
					projection_views[eye].subImage.imageRect.extent.height = (int32_t)eye_height[eye];
				}
				projection_layer.space = local_space;
				projection_layer.viewCount = 2;
				projection_layer.views = projection_views;
				layers[0] = (const XrCompositionLayerBaseHeader *)&projection_layer;
				layer_count = 1;
				frames_with_projection_layer++;
			} else {
				fprintf(stderr, "[openxr-probe] shouldRender=true without valid stereo pose; ending frame with zero layers\n");
			}
		} else {
			standby_zero_layer_frames++;
		}

		XrFrameEndInfo end_info = { XR_TYPE_FRAME_END_INFO };
		end_info.displayTime = frame_state.predictedDisplayTime;
		end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		end_info.layerCount = layer_count;
		end_info.layers = layer_count ? layers : NULL;
		XR_TRY(xrEndFrame(session, &end_info));
		if (layer_count == 0)
			frames_zero_layers++;
		frame_cpu_ms_sum += (monotonic_seconds() - frame_start) * 1000.0;
		frame_cpu_samples++;
	}

	exit_code = frames_with_projection_layer > 0 ? 0 : 2;
	printf("[frames] waited=%llu should_render=%llu valid_pose=%llu projection_layer=%llu "
		"zero_layers=%llu standby_zero_layer=%llu display_period=%.3fms avg_frame_cpu=%.3fms\n",
		(unsigned long long)frames_waited, (unsigned long long)frames_should_render,
		(unsigned long long)frames_with_valid_pose, (unsigned long long)frames_with_projection_layer,
		(unsigned long long)frames_zero_layers, (unsigned long long)standby_zero_layer_frames,
		(double)last_predicted_period * 1e-6,
		frame_cpu_samples ? frame_cpu_ms_sum / (double)frame_cpu_samples : 0.0);
	if (frames_waited > 1 && last_predicted_time > first_predicted_time) {
		double span = (double)(last_predicted_time - first_predicted_time) * 1e-9;
		printf("[frames] predicted_rate=%.2fHz\n", (double)(frames_waited - 1) / span);
	}
	printf("[openxr-probe] %s\n", exit_code == 0 ? "PASS" : "NO PROJECTION LAYER RENDERED");

cleanup:
	if (session != XR_NULL_HANDLE && session_running) {
		XrResult stop_result = xrRequestExitSession(session);
		if (XR_FAILED(stop_result))
			report_xr_failure("xrRequestExitSession(cleanup)", stop_result, __LINE__);
		else {
			double deadline = monotonic_seconds() + 2.0;
			while (session_running && monotonic_seconds() < deadline) {
				XrEventDataBuffer event_buffer = { XR_TYPE_EVENT_DATA_BUFFER };
				while (xrPollEvent(instance, &event_buffer) == XR_SUCCESS) {
					if (event_buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
						const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event_buffer;
						if (changed->state == XR_SESSION_STATE_STOPPING) {
							XrResult end_result = xrEndSession(session);
							if (XR_FAILED(end_result))
								report_xr_failure("xrEndSession(cleanup)", end_result, __LINE__);
							else
								session_running = 0;
						}
					}
					event_buffer.type = XR_TYPE_EVENT_DATA_BUFFER;
				}
				SDL_Delay(5);
			}
			if (session_running)
				fprintf(stderr, "[openxr-probe] timed out waiting for STOPPING during cleanup\n");
		}
	}
	for (uint32_t eye = 0; eye < 2; eye++) {
		if (swapchains[eye] != XR_NULL_HANDLE) {
			XrResult destroy_result = xrDestroySwapchain(swapchains[eye]);
			if (XR_FAILED(destroy_result))
				report_xr_failure("xrDestroySwapchain", destroy_result, __LINE__);
		}
		free(swapchain_images[eye]);
	}
	if (local_space != XR_NULL_HANDLE) {
		XrResult destroy_result = xrDestroySpace(local_space);
		if (XR_FAILED(destroy_result))
			report_xr_failure("xrDestroySpace", destroy_result, __LINE__);
	}
	if (session != XR_NULL_HANDLE) {
		XrResult destroy_result = xrDestroySession(session);
		if (XR_FAILED(destroy_result))
			report_xr_failure("xrDestroySession", destroy_result, __LINE__);
	}
	if (instance != XR_NULL_HANDLE) {
		XrResult destroy_result = xrDestroyInstance(instance);
		if (XR_FAILED(destroy_result))
			report_xr_failure("xrDestroyInstance", destroy_result, __LINE__);
		instance = XR_NULL_HANDLE;
	}
	free(view_configuration_views);
	free(formats);
	free(view_types);
	free(blend_modes);
	free(reference_spaces);
	if (framebuffer && gl_delete_framebuffers)
		gl_delete_framebuffers(1, &framebuffer);
	if (gl_context)
		SDL_GL_DeleteContext(gl_context);
	if (window)
		SDL_DestroyWindow(window);
	if (sdl_initialized)
		SDL_Quit();
	return exit_code;
}
