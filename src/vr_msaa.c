#include "vr_msaa.h"
#include <stdio.h>
#include <string.h>

static int api_ready(const vr_msaa_gl_api_t *gl)
{
    if (!gl) {
        fprintf(stderr, "[vr] fixed 4x MSAA GL API is missing\n");
        return 0;
    }
#define REQUIRE_GL(field)                                                                                   \
    do {                                                                                                     \
        if (!gl->field) {                                                                                    \
            fprintf(stderr, "[vr] fixed 4x MSAA GL API has no %s\n", #field);                               \
            return 0;                                                                                        \
        }                                                                                                    \
    } while (0)
    REQUIRE_GL(get_integer);
    REQUIRE_GL(gen_framebuffers);
    REQUIRE_GL(delete_framebuffers);
    REQUIRE_GL(bind_framebuffer);
    REQUIRE_GL(check_framebuffer_status);
    REQUIRE_GL(gen_renderbuffers);
    REQUIRE_GL(delete_renderbuffers);
    REQUIRE_GL(bind_renderbuffer);
    REQUIRE_GL(renderbuffer_storage_multisample);
    REQUIRE_GL(get_renderbuffer_parameter);
    REQUIRE_GL(framebuffer_renderbuffer);
    REQUIRE_GL(draw_buffer);
    REQUIRE_GL(read_buffer);
    REQUIRE_GL(is_enabled);
    REQUIRE_GL(enable);
    REQUIRE_GL(disable);
    REQUIRE_GL(blit_framebuffer);
#undef REQUIRE_GL
    return 1;
}

void vr_msaa_target_destroy(const vr_msaa_gl_api_t *gl, vr_msaa_target_t *target)
{
    if (!target)
        return;
    if (gl && gl->delete_renderbuffers) {
        GLuint renderbuffers[2] = {target->colour, target->depth};
        if (renderbuffers[0] || renderbuffers[1])
            gl->delete_renderbuffers(2, renderbuffers);
    }
    if (target->framebuffer && gl && gl->delete_framebuffers)
        gl->delete_framebuffers(1, &target->framebuffer);
    memset(target, 0, sizeof(*target));
}

int vr_msaa_target_create(const vr_msaa_gl_api_t *gl, vr_msaa_target_t *target, int width, int height)
{
    if (!target)
        return 0;
    memset(target, 0, sizeof(*target));
    if (!api_ready(gl))
        return 0;
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "[vr] fixed 4x MSAA eye target unavailable: invalid dimensions; aborting\n");
        return 0;
    }

    GLint previous_read = 0, previous_draw = 0, previous_renderbuffer = 0, max_samples = 0;
    gl->get_integer(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
    gl->get_integer(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
    gl->get_integer(GL_RENDERBUFFER_BINDING, &previous_renderbuffer);
    gl->get_integer(GL_MAX_SAMPLES, &max_samples);
    if (max_samples < VR_EYE_MSAA_SAMPLES) {
        fprintf(stderr, "[vr] fixed 4x MSAA eye target unavailable: GL_MAX_SAMPLES=%d; aborting\n",
                max_samples);
        return 0;
    }

    gl->gen_framebuffers(1, &target->framebuffer);
    gl->gen_renderbuffers(1, &target->colour);
    gl->gen_renderbuffers(1, &target->depth);
    if (!target->framebuffer || !target->colour || !target->depth) {
        fprintf(stderr, "[vr] fixed 4x MSAA eye target unavailable: GL object allocation failed; aborting\n");
        goto fail;
    }

    gl->bind_framebuffer(GL_FRAMEBUFFER, target->framebuffer);
    gl->bind_renderbuffer(GL_RENDERBUFFER, target->colour);
    gl->renderbuffer_storage_multisample(GL_RENDERBUFFER, VR_EYE_MSAA_SAMPLES, GL_SRGB8_ALPHA8,
                                         width, height);
    GLint colour_samples = 0;
    gl->get_renderbuffer_parameter(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &colour_samples);

    gl->bind_renderbuffer(GL_RENDERBUFFER, target->depth);
    gl->renderbuffer_storage_multisample(GL_RENDERBUFFER, VR_EYE_MSAA_SAMPLES, GL_DEPTH_COMPONENT24,
                                         width, height);
    GLint depth_samples = 0;
    gl->get_renderbuffer_parameter(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &depth_samples);

    gl->bind_framebuffer(GL_FRAMEBUFFER, target->framebuffer);
    gl->framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, target->colour);
    gl->framebuffer_renderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target->depth);
    gl->draw_buffer(GL_COLOR_ATTACHMENT0);
    gl->read_buffer(GL_COLOR_ATTACHMENT0);
    GLenum status = gl->check_framebuffer_status(GL_FRAMEBUFFER);
    if (colour_samples != VR_EYE_MSAA_SAMPLES || depth_samples != VR_EYE_MSAA_SAMPLES ||
        status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr,
                "[vr] fixed 4x MSAA eye target unavailable: color_samples=%d depth_samples=%d "
                "framebuffer_status=0x%04x; aborting\n",
                colour_samples, depth_samples, status);
        goto fail;
    }

    target->width = width;
    target->height = height;
    gl->bind_framebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
    gl->bind_framebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw);
    gl->bind_renderbuffer(GL_RENDERBUFFER, (GLuint)previous_renderbuffer);
    printf("[vr] fixed 4x MSAA eye target %dx%d color=sRGB8-alpha8 depth=24 samples=%d\n", width,
           height, VR_EYE_MSAA_SAMPLES);
    return 1;

fail:
    vr_msaa_target_destroy(gl, target);
    gl->bind_framebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
    gl->bind_framebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw);
    gl->bind_renderbuffer(GL_RENDERBUFFER, (GLuint)previous_renderbuffer);
    return 0;
}

int vr_msaa_target_resolve(const vr_msaa_gl_api_t *gl, const vr_msaa_target_t *target,
                           GLuint destination_framebuffer, int width, int height)
{
    if (!api_ready(gl) || !target || !target->framebuffer || !destination_framebuffer ||
        width != target->width || height != target->height) {
        fprintf(stderr, "[vr] fixed 4x MSAA eye resolve unavailable: invalid target or extent; aborting\n");
        return 0;
    }

    GLboolean srgb_was_enabled = gl->is_enabled(GL_FRAMEBUFFER_SRGB);
    gl->bind_framebuffer(GL_READ_FRAMEBUFFER, target->framebuffer);
    gl->read_buffer(GL_COLOR_ATTACHMENT0);
    gl->bind_framebuffer(GL_DRAW_FRAMEBUFFER, destination_framebuffer);
    gl->draw_buffer(GL_COLOR_ATTACHMENT0);
    /* Both buffers are GL_SRGB8_ALPHA8. Keeping sRGB enabled for this blit lets
       Zink fold the resolve into the tiled render pass while preserving stored
       gamma-encoded eye colours. */
    gl->enable(GL_FRAMEBUFFER_SRGB);
    gl->blit_framebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (gl->invalidate_framebuffer) {
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
        gl->invalidate_framebuffer(GL_READ_FRAMEBUFFER, 2, attachments);
    }
    gl->bind_framebuffer(GL_FRAMEBUFFER, destination_framebuffer);
    if (!srgb_was_enabled)
        gl->disable(GL_FRAMEBUFFER_SRGB);
    return 1;
}
