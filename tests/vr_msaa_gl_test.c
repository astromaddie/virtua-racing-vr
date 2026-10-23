#include "gl.h"
#include "vr_msaa.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { WIDTH = 96, HEIGHT = 96 };

typedef struct {
    float x, y, z, r, g, b;
} vertex_t;

static const char *vertex_shader_source =
    "#version 330 core\n"
    "layout(location=0) in vec3 position;\n"
    "layout(location=1) in vec3 colour;\n"
    "out vec3 fragment_colour;\n"
    "void main() { gl_Position = vec4(position, 1.0); fragment_colour = colour; }\n";
static const char *fragment_shader_source =
    "#version 330 core\n"
    "in vec3 fragment_colour;\n"
    "out vec4 out_colour;\n"
    "void main() { out_colour = vec4(fragment_colour, 1.0); }\n";

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

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[2048] = "";
        GLsizei written = 0;
        glGetShaderInfoLog(shader, sizeof(log), &written, log);
        fprintf(stderr, "shader compile failed: %s\n", log);
        exit(1);
    }
    return shader;
}

static GLuint make_program(void)
{
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    GLint linked = 0;
    PFNGLGETPROGRAMIVPROC get_program_iv =
        (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    PFNGLGETPROGRAMINFOLOGPROC get_program_log =
        (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    assert(get_program_iv && get_program_log);
    get_program_iv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048] = "";
        GLsizei written = 0;
        get_program_log(program, sizeof(log), &written, log);
        fprintf(stderr, "shader link failed: %s\n", log);
        exit(1);
    }
    return program;
}

static GLuint make_sample_count_one_fbo(GLuint *colour_texture, GLuint *depth_renderbuffer)
{
    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenTextures(1, colour_texture);
    glBindTexture(GL_TEXTURE_2D, *colour_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *colour_texture, 0);

    glGenRenderbuffers(1, depth_renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, *depth_renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, WIDTH, HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth_renderbuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    return framebuffer;
}

static const vertex_t scene[] = {
    /* Far red triangle with a slanted edge. */
    {-0.87f, -0.75f, 0.45f, 0.76f, 0.16f, 0.08f},
    {0.86f, -0.48f, 0.45f, 0.76f, 0.16f, 0.08f},
    {-0.23f, 0.86f, 0.45f, 0.76f, 0.16f, 0.08f},
    /* Near blue triangle overlaps the red one and must win the depth test. */
    {-0.46f, -0.24f, -0.25f, 0.10f, 0.30f, 0.86f},
    {0.53f, -0.12f, -0.25f, 0.10f, 0.30f, 0.86f},
    {-0.10f, 0.57f, -0.25f, 0.10f, 0.30f, 0.86f},
};

static void render(GLuint framebuffer, GLuint program, GLuint vao, GLuint buffer, int samples)
{
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, WIDTH, HEIGHT);
    glDisable(GL_FRAMEBUFFER_SRGB); /* Palette values are already gamma encoded. */
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    if (samples > 1)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);
    glClearColor(0.04f, 0.06f, 0.08f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(scene), scene, GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_DEPTH_TEST);
}

static void read_pixels(GLuint framebuffer, unsigned char pixels[HEIGHT][WIDTH][4])
{
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static int near_colour(const unsigned char *actual, const unsigned char expected[3], int tolerance)
{
    for (int channel = 0; channel < 3; channel++)
        if (abs((int)actual[channel] - expected[channel]) > tolerance)
            return 0;
    return 1;
}

static void require_colour(const char *label, const unsigned char *actual, const unsigned char expected[3])
{
    if (!near_colour(actual, expected, 2)) {
        fprintf(stderr, "%s colour was [%u,%u,%u], expected [%u,%u,%u]\n", label,
                actual[0], actual[1], actual[2], expected[0], expected[1], expected[2]);
        exit(1);
    }
}

static void expect_no_gl_error(const char *stage)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "OpenGL error 0x%04x after %s\n", error, stage);
        exit(1);
    }
}

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
    SDL_Window *window = SDL_CreateWindow("VR MSAA validation", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    assert(context);
    assert(gl_load_functions());
    vr_msaa_gl_api_t api = msaa_api_current();
    if (!api.invalidate_framebuffer) {
        fprintf(stderr, "OpenGL validation context has no glInvalidateFramebuffer\n");
        return 1;
    }

    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    printf("OpenGL validation context %d.%d renderer=%s invalidate=available\n", major, minor,
           glGetString(GL_RENDERER));

    GLuint program = make_program(), vao = 0, buffer = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(scene), scene, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (const void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (const void *)(3 * sizeof(float)));

    vr_msaa_target_t multisample;
    assert(vr_msaa_target_create(&api, &multisample, WIDTH, HEIGHT));
    GLuint direct_texture = 0, direct_depth = 0;
    GLuint direct_fbo = make_sample_count_one_fbo(&direct_texture, &direct_depth);
    GLuint resolved_texture = 0, resolved_depth = 0;
    GLuint resolved_fbo = make_sample_count_one_fbo(&resolved_texture, &resolved_depth);
    expect_no_gl_error("FBO creation");

    unsigned char direct[HEIGHT][WIDTH][4];
    unsigned char resolved[HEIGHT][WIDTH][4];
    render(direct_fbo, program, vao, buffer, 1);
    read_pixels(direct_fbo, direct);
    render(multisample.framebuffer, program, vao, buffer, VR_EYE_MSAA_SAMPLES);
    assert(vr_msaa_target_resolve(&api, &multisample, resolved_fbo, WIDTH, HEIGHT));
    read_pixels(resolved_fbo, resolved);
    expect_no_gl_error("direct draw, multisample draw, resolve and readback");

    const unsigned char clear_colour[3] = {10, 15, 20};
    const unsigned char red_colour[3] = {194, 41, 20};
    const unsigned char blue_colour[3] = {26, 77, 219};
    require_colour("direct red interior", direct[18][22], red_colour);
    require_colour("resolved red interior", resolved[18][22], red_colour);
    require_colour("direct depth overlap", direct[48][48], blue_colour);
    require_colour("resolved depth overlap", resolved[48][48], blue_colour);
    require_colour("resolved clear", resolved[6][88], clear_colour);

    unsigned mixed_edges = 0;
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++) {
            const unsigned char *direct_sample = direct[y][x];
            const unsigned char *sample = resolved[y][x];
            int direct_palette_pixel = near_colour(direct_sample, clear_colour, 2) ||
                                       near_colour(direct_sample, red_colour, 2) ||
                                       near_colour(direct_sample, blue_colour, 2);
            int palette_pixel = near_colour(sample, clear_colour, 2) ||
                                near_colour(sample, red_colour, 2) ||
                                near_colour(sample, blue_colour, 2);
            assert(direct_palette_pixel);
            if (!palette_pixel)
                mixed_edges++;
        }
    if (!mixed_edges) {
        fprintf(stderr, "MSAA resolve produced no mixed-coverage edge pixels\n");
        return 1;
    }
    printf("Interior palette and depth overlap match; mixed-coverage pixels=%u\n", mixed_edges);

    vr_msaa_target_destroy(&api, &multisample);
    glDeleteRenderbuffers(1, &direct_depth);
    glDeleteRenderbuffers(1, &resolved_depth);
    glDeleteTextures(1, &direct_texture);
    glDeleteTextures(1, &resolved_texture);
    glDeleteFramebuffers(1, &direct_fbo);
    glDeleteFramebuffers(1, &resolved_fbo);
    expect_no_gl_error("resource cleanup");
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
