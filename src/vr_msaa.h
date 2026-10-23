#ifndef VR_MSAA_H
#define VR_MSAA_H

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <SDL.h>
#include <SDL_opengl.h>
#endif

enum { VR_EYE_MSAA_SAMPLES = 2 };

typedef struct {
    GLuint framebuffer;
    GLuint colour;
    GLuint depth;
    int width;
    int height;
} vr_msaa_target_t;

/* Kept injectable so the target allocation/resolve path can be exercised without
   requiring a platform GL context. Production supplies the current context's GL
   entry points. */
typedef struct {
    void (*get_integer)(GLenum name, GLint *value);
    void (*gen_framebuffers)(GLsizei count, GLuint *ids);
    void (*delete_framebuffers)(GLsizei count, const GLuint *ids);
    void (*bind_framebuffer)(GLenum target, GLuint framebuffer);
    GLenum (*check_framebuffer_status)(GLenum target);
    void (*gen_renderbuffers)(GLsizei count, GLuint *ids);
    void (*delete_renderbuffers)(GLsizei count, const GLuint *ids);
    void (*bind_renderbuffer)(GLenum target, GLuint renderbuffer);
    void (*renderbuffer_storage_multisample)(GLenum target, GLsizei samples, GLenum format,
                                             GLsizei width, GLsizei height);
    void (*get_renderbuffer_parameter)(GLenum target, GLenum name, GLint *value);
    void (*framebuffer_renderbuffer)(GLenum target, GLenum attachment, GLenum renderbuffer_target,
                                     GLuint renderbuffer);
    void (*draw_buffer)(GLenum buffer);
    void (*read_buffer)(GLenum buffer);
    GLboolean (*is_enabled)(GLenum capability);
    void (*enable)(GLenum capability);
    void (*disable)(GLenum capability);
    void (*blit_framebuffer)(GLint src_x0, GLint src_y0, GLint src_x1, GLint src_y1,
                             GLint dst_x0, GLint dst_y0, GLint dst_x1, GLint dst_y1,
                             GLbitfield mask, GLenum filter);
    /* OpenGL 4.3; optional so older flat-renderer contexts need not expose it. */
    void (*invalidate_framebuffer)(GLenum target, GLsizei count, const GLenum *attachments);
} vr_msaa_gl_api_t;

int vr_msaa_target_create(const vr_msaa_gl_api_t *gl, vr_msaa_target_t *target, int width, int height);
void vr_msaa_target_destroy(const vr_msaa_gl_api_t *gl, vr_msaa_target_t *target);
/* Resolves colour only and leaves destination_framebuffer bound for readback. */
int vr_msaa_target_resolve(const vr_msaa_gl_api_t *gl, const vr_msaa_target_t *target,
                           GLuint destination_framebuffer, int width, int height);

#endif
