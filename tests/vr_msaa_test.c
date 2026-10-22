#include "vr_msaa.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    GLint read_framebuffer;
    GLint draw_framebuffer;
    GLint renderbuffer;
    GLint max_samples;
    GLint colour_samples;
    GLint depth_samples;
    GLint colour_storage_samples;
    GLint depth_storage_samples;
    GLenum colour_format;
    GLenum depth_format;
    GLsizei colour_width, colour_height, depth_width, depth_height;
    GLenum framebuffer_status;
    GLuint next_framebuffer, next_renderbuffer;
    GLuint attached_colour, attached_depth;
    GLenum draw_buffer, read_buffer;
    GLboolean srgb_enabled;
    unsigned framebuffer_allocations, renderbuffer_allocations, deleted_framebuffers,
        deleted_renderbuffers, blits;
    GLint blit_src_x0, blit_src_y0, blit_src_x1, blit_src_y1;
    GLint blit_dst_x0, blit_dst_y0, blit_dst_x1, blit_dst_y1;
    GLbitfield blit_mask;
    GLenum blit_filter;
    GLboolean srgb_during_blit;
    GLuint invalidate_read_framebuffer;
    GLenum invalidate_target;
    GLsizei invalidate_count;
    GLenum invalidate_attachments[2];
    unsigned operations, blit_order, invalidate_order, destination_bind_order;
} fake_gl_t;

static fake_gl_t fake;

static void get_integer(GLenum name, GLint *value)
{
    switch (name) {
    case GL_READ_FRAMEBUFFER_BINDING: *value = fake.read_framebuffer; break;
    case GL_DRAW_FRAMEBUFFER_BINDING: *value = fake.draw_framebuffer; break;
    case GL_RENDERBUFFER_BINDING: *value = fake.renderbuffer; break;
    case GL_MAX_SAMPLES: *value = fake.max_samples; break;
    default: assert(!"unexpected integer query");
    }
}
static void gen_framebuffers(GLsizei count, GLuint *ids)
{
    fake.framebuffer_allocations += count;
    while (count--)
        *ids++ = ++fake.next_framebuffer;
}
static void delete_framebuffers(GLsizei count, const GLuint *ids)
{
    (void)ids;
    fake.deleted_framebuffers += count;
}
static void bind_framebuffer(GLenum target, GLuint id)
{
    if (fake.blits && target == GL_FRAMEBUFFER)
        fake.destination_bind_order = ++fake.operations;
    if (target == GL_FRAMEBUFFER)
        fake.read_framebuffer = fake.draw_framebuffer = id;
    else if (target == GL_READ_FRAMEBUFFER)
        fake.read_framebuffer = id;
    else if (target == GL_DRAW_FRAMEBUFFER)
        fake.draw_framebuffer = id;
    else
        assert(!"unexpected framebuffer target");
}
static GLenum check_framebuffer_status(GLenum target)
{
    assert(target == GL_FRAMEBUFFER);
    return fake.framebuffer_status;
}
static void gen_renderbuffers(GLsizei count, GLuint *ids)
{
    fake.renderbuffer_allocations += count;
    while (count--)
        *ids++ = ++fake.next_renderbuffer;
}
static void delete_renderbuffers(GLsizei count, const GLuint *ids)
{
    (void)ids;
    fake.deleted_renderbuffers += count;
}
static void bind_renderbuffer(GLenum target, GLuint id)
{
    assert(target == GL_RENDERBUFFER);
    fake.renderbuffer = id;
}
static void storage_multisample(GLenum target, GLsizei samples, GLenum format, GLsizei width,
                                GLsizei height)
{
    assert(target == GL_RENDERBUFFER);
    if (fake.renderbuffer == fake.next_renderbuffer - 1) {
        fake.colour_storage_samples = samples;
        fake.colour_format = format;
        fake.colour_width = width;
        fake.colour_height = height;
    } else {
        assert(fake.renderbuffer == fake.next_renderbuffer);
        fake.depth_storage_samples = samples;
        fake.depth_format = format;
        fake.depth_width = width;
        fake.depth_height = height;
    }
}
static void get_renderbuffer_parameter(GLenum target, GLenum name, GLint *value)
{
    assert(target == GL_RENDERBUFFER && name == GL_RENDERBUFFER_SAMPLES);
    *value = fake.renderbuffer == fake.next_renderbuffer - 1 ? fake.colour_samples : fake.depth_samples;
}
static void framebuffer_renderbuffer(GLenum target, GLenum attachment, GLenum renderbuffer_target,
                                     GLuint renderbuffer)
{
    assert(target == GL_FRAMEBUFFER && renderbuffer_target == GL_RENDERBUFFER);
    if (attachment == GL_COLOR_ATTACHMENT0)
        fake.attached_colour = renderbuffer;
    else {
        assert(attachment == GL_DEPTH_ATTACHMENT);
        fake.attached_depth = renderbuffer;
    }
}
static void draw_buffer(GLenum buffer) { fake.draw_buffer = buffer; }
static void read_buffer(GLenum buffer) { fake.read_buffer = buffer; }
static GLboolean is_enabled(GLenum capability)
{
    assert(capability == GL_FRAMEBUFFER_SRGB);
    return fake.srgb_enabled;
}
static void enable(GLenum capability)
{
    assert(capability == GL_FRAMEBUFFER_SRGB);
    fake.srgb_enabled = GL_TRUE;
}
static void disable(GLenum capability)
{
    assert(capability == GL_FRAMEBUFFER_SRGB);
    fake.srgb_enabled = GL_FALSE;
}
static void blit_framebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,
                             GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{
    fake.blits++;
    fake.blit_order = ++fake.operations;
    fake.blit_src_x0 = sx0;
    fake.blit_src_y0 = sy0;
    fake.blit_src_x1 = sx1;
    fake.blit_src_y1 = sy1;
    fake.blit_dst_x0 = dx0;
    fake.blit_dst_y0 = dy0;
    fake.blit_dst_x1 = dx1;
    fake.blit_dst_y1 = dy1;
    fake.blit_mask = mask;
    fake.blit_filter = filter;
    fake.srgb_during_blit = fake.srgb_enabled;
}
static void invalidate_framebuffer(GLenum target, GLsizei count, const GLenum *attachments)
{
    fake.invalidate_order = ++fake.operations;
    fake.invalidate_target = target;
    fake.invalidate_count = count;
    assert(count == 2);
    fake.invalidate_attachments[0] = attachments[0];
    fake.invalidate_attachments[1] = attachments[1];
    fake.invalidate_read_framebuffer = fake.read_framebuffer;
}

static const vr_msaa_gl_api_t api = {
    get_integer,
    gen_framebuffers,
    delete_framebuffers,
    bind_framebuffer,
    check_framebuffer_status,
    gen_renderbuffers,
    delete_renderbuffers,
    bind_renderbuffer,
    storage_multisample,
    get_renderbuffer_parameter,
    framebuffer_renderbuffer,
    draw_buffer,
    read_buffer,
    is_enabled,
    enable,
    disable,
    blit_framebuffer,
    invalidate_framebuffer,
};

static void setup(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.read_framebuffer = 31;
    fake.draw_framebuffer = 32;
    fake.renderbuffer = 33;
    fake.max_samples = 8;
    fake.colour_samples = fake.depth_samples = 4;
    fake.framebuffer_status = GL_FRAMEBUFFER_COMPLETE;
    fake.next_framebuffer = 100;
    fake.next_renderbuffer = 200;
}

static void test_fixed_target_and_resolve(void)
{
    setup();
    vr_msaa_target_t target;
    assert(vr_msaa_target_create(&api, &target, 2160, 2160));
    assert(target.width == 2160 && target.height == 2160);
    assert(target.framebuffer == 101 && target.colour == 201 && target.depth == 202);
    assert(fake.colour_storage_samples == 4 && fake.depth_storage_samples == 4);
    assert(fake.colour_format == GL_SRGB8_ALPHA8 && fake.depth_format == GL_DEPTH_COMPONENT24);
    assert(fake.colour_width == 2160 && fake.colour_height == 2160);
    assert(fake.depth_width == 2160 && fake.depth_height == 2160);
    assert(fake.attached_colour == target.colour && fake.attached_depth == target.depth);
    assert(fake.draw_buffer == GL_COLOR_ATTACHMENT0 && fake.read_buffer == GL_COLOR_ATTACHMENT0);
    assert(fake.read_framebuffer == 31 && fake.draw_framebuffer == 32 && fake.renderbuffer == 33);

    assert(vr_msaa_target_resolve(&api, &target, 44, 2160, 2160));
    assert(fake.blits == 1);
    assert(fake.blit_src_x0 == 0 && fake.blit_src_y0 == 0 && fake.blit_src_x1 == 2160 &&
           fake.blit_src_y1 == 2160);
    assert(fake.blit_dst_x0 == 0 && fake.blit_dst_y0 == 0 && fake.blit_dst_x1 == 2160 &&
           fake.blit_dst_y1 == 2160);
    assert(fake.blit_mask == GL_COLOR_BUFFER_BIT && fake.blit_filter == GL_NEAREST);
    assert(fake.srgb_during_blit && !fake.srgb_enabled);
    assert(fake.invalidate_target == GL_READ_FRAMEBUFFER);
    assert(fake.invalidate_count == 2);
    assert(fake.invalidate_attachments[0] == GL_COLOR_ATTACHMENT0);
    assert(fake.invalidate_attachments[1] == GL_DEPTH_ATTACHMENT);
    assert(fake.invalidate_read_framebuffer == target.framebuffer);
    assert(fake.blit_order < fake.invalidate_order);
    assert(fake.invalidate_order < fake.destination_bind_order);
    assert(fake.read_framebuffer == 44 && fake.draw_framebuffer == 44);
    assert(fake.read_buffer == GL_COLOR_ATTACHMENT0 && fake.draw_buffer == GL_COLOR_ATTACHMENT0);

    fake.srgb_enabled = GL_TRUE;
    assert(vr_msaa_target_resolve(&api, &target, 45, 2160, 2160));
    assert(fake.srgb_during_blit && fake.srgb_enabled);
    assert(!vr_msaa_target_resolve(&api, &target, 45, 2159, 2160));
    assert(fake.blits == 2);

    vr_msaa_gl_api_t without_invalidation = api;
    without_invalidation.invalidate_framebuffer = NULL;
    fake.invalidate_order = 0;
    assert(vr_msaa_target_resolve(&without_invalidation, &target, 46, 2160, 2160));
    assert(fake.blits == 3 && fake.invalidate_order == 0);

    vr_msaa_target_destroy(&api, &target);
    assert(fake.deleted_framebuffers == 1 && fake.deleted_renderbuffers == 2);
    assert(!target.framebuffer && !target.colour && !target.depth && !target.width && !target.height);
}

static void test_unsupported_or_incomplete_target_aborts(void)
{
    setup();
    vr_msaa_target_t target;
    fake.max_samples = 2;
    assert(!vr_msaa_target_create(&api, &target, 2160, 2160));
    assert(fake.framebuffer_allocations == 0 && fake.renderbuffer_allocations == 0);

    setup();
    fake.colour_samples = 2;
    assert(!vr_msaa_target_create(&api, &target, 2160, 2160));
    assert(!target.framebuffer && !target.colour && !target.depth);
    assert(fake.deleted_framebuffers == 1 && fake.deleted_renderbuffers == 2);
    assert(fake.read_framebuffer == 31 && fake.draw_framebuffer == 32 && fake.renderbuffer == 33);

    setup();
    fake.framebuffer_status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    assert(!vr_msaa_target_create(&api, &target, 2160, 2160));
    assert(fake.deleted_framebuffers == 1 && fake.deleted_renderbuffers == 2);
}

int main(void)
{
    test_fixed_target_and_resolve();
    test_unsupported_or_incomplete_target_aborts();
    puts("VR fixed 4x MSAA target tests passed");
    return 0;
}
