/* VR-only integration with the existing front end. Included after its helpers so
   the exact existing simulation, audio, palette and input routes remain shared. */
#include "vr_math.h"
#include "vr_xr.h"
static int vr_mode, vr_diagnostic;
static const hle_vr_mesh_t *vr_scene;
static GLuint vr_program, vr_sky_program, vr_vao, vr_buffer, vr_panel_texture, vr_panel_program;
static int vr_immersive;
static long vr_sim_frames, vr_display_frames;
static unsigned long long vr_audio_hash = 1469598103934665603ULL, vr_video_hash = 1469598103934665603ULL;
static double vr_previous, vr_accumulator;
static int vr_script_length, *vr_script_frames;
static uint16_t *vr_script_buttons;
static int vr_dumped;
static const char *vr_vertex_source =
    "#version 330 core\n"
    "layout(location=0) in vec3 position; layout(location=1) in uint colour;\n"
    "uniform mat4 matrix; flat out uint palette_index; flat out uint dither;\n"
    "void main(){gl_Position=matrix*vec4(position,1);palette_index=colour>>8;dither=colour&2u;}\n";
static const char *vr_fragment_source =
    "#version 330 core\n"
    "uniform usampler2D palette; flat in uint palette_index; flat in uint dither; out vec4 color;\n"
    "void main(){if(dither!=0u && ((int(gl_FragCoord.x)+int(gl_FragCoord.y))&1)!=0)discard;\n"
    "uint c=texelFetch(palette,ivec2(int(palette_index),112),0).r;\n"
    "color=vec4(vec3(float(c&31u),float((c>>5)&31u),float((c>>10)&31u))/31.0,1);}\n";
static const char *vr_sky_source =
    "#version 330 core\n"
    "in vec2 uv;out vec4 color;uniform sampler2D sky;uniform mat4 rotation;uniform mat4 bounds;uniform float sky_period;\n"
    "void main(){vec3 ray=vec3(mix(bounds[0].x,bounds[0].y,uv.x),mix(bounds[0].w,bounds[0].z,uv.y),-1);\n"
    "ray=(rotation*vec4(ray,0)).xyz;float yaw=atan(ray.x,-ray.z);float "
    "elevation=atan(ray.y,length(ray.xz));\n"
    "float x=544.0+mod(yaw*sky_period/6.28318530718+sky_period*0.5,sky_period)-sky_period*0.5;float y=clamp(112.0-elevation*112.0,16.0,207.0);\n"
    "color=vec4(texelFetch(sky,ivec2(int(x),int(y)),0).rgb,1);}\n";
static void vr_graphics_init(void)
{
    vr_program = link_program(vr_vertex_source, vr_fragment_source);
    glUseProgram(vr_program);
    set_int(vr_program, "palette", UNIT_X32_PALETTE);
    vr_sky_program = link_program(quad_vertex_shader, vr_sky_source);
    glUseProgram(vr_sky_program);
    set_int(vr_sky_program, "sky", UNIT_MD_WIDE);
    set_vec2(vr_sky_program, "uv_scale", 1, 1);
    vr_panel_program = link_program(quad_vertex_shader, blit_shader);
    glUseProgram(vr_panel_program);
    set_int(vr_panel_program, "tex", 15);
    set_vec2(vr_panel_program, "uv_scale", 1, 1);
    glGenVertexArrays(1, &vr_vao);
    glBindVertexArray(vr_vao);
    glGenBuffers(1, &vr_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vr_buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(hle_vr_vertex_t), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(hle_vr_vertex_t),
                           (void *)offsetof(hle_vr_vertex_t, colour));
    glActiveTexture(GL_TEXTURE15);
    vr_panel_texture = make_texture(GL_RGBA8, 320, 224, GL_RGBA, GL_UNSIGNED_BYTE);
    glActiveTexture(GL_TEXTURE0);
}
static void vr_hash(unsigned long long *hash, const void *data, size_t bytes)
{
    const unsigned char *p = data;
    while (bytes--) {
        *hash ^= *p++;
        *hash *= 1099511628211ULL;
    }
}
/* 64-bit FNV-1a: a simple hash; any difference in the data changes it. */
#define HASH_START 0xcbf29ce484222325ull

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static uint64_t hash_of(const void *data, size_t size) { return hash_bytes(HASH_START, data, size); }

static uint64_t hash_visible_screen(void)
{
    const uint16_t *first_visible_row = &core_screen[core_visible_top * CORE_SCREEN_WIDTH];
    size_t visible_bytes = (size_t)core_visible_lines * CORE_SCREEN_WIDTH * sizeof(uint16_t);
    return hash_of(first_visible_row, visible_bytes);
}

static uint64_t hash_vdp_memory(void)
{
    uint64_t hash = HASH_START;
    hash = hash_bytes(hash, PicoMem.vram, sizeof(PicoMem.vram));
    hash = hash_bytes(hash, PicoMem.cram, sizeof(PicoMem.cram));
    hash = hash_bytes(hash, PicoMem.vsram, sizeof(PicoMem.vsram));
    return hash;
}

/* The 32X memory is allocated when the game first switches the 32X on; before that
   these hash to 0. */
static uint64_t hash_sdram(void)
{
    if (Pico32xMem == NULL)
        return 0;
    return hash_of(Pico32xMem->sdram, sizeof(Pico32xMem->sdram));
}

static uint64_t hash_32x_framebuffers(void)
{
    if (Pico32xMem == NULL)
        return 0;
    uint64_t hash = HASH_START;
    hash = hash_bytes(hash, Pico32xMem->dram, sizeof(Pico32xMem->dram));
    hash = hash_bytes(hash, Pico32xMem->pal, sizeof(Pico32xMem->pal));
    return hash;
}

static void print_frame_hashes(int frame)
{
    size_t audio_bytes = (size_t)core_audio_frame_count * 2 * sizeof(int16_t);

    printf("F %06d", frame);
    printf(" screen %016llx", (unsigned long long)hash_visible_screen());
    printf(" audio %016llx", (unsigned long long)hash_of(core_audio_samples, audio_bytes));
    printf(" ram68k %016llx", (unsigned long long)hash_of(PicoMem.ram, sizeof(PicoMem.ram)));
    printf(" vdp %016llx", (unsigned long long)hash_vdp_memory());
    printf(" sdram %016llx", (unsigned long long)hash_sdram());
    printf(" framebuffer %016llx", (unsigned long long)hash_32x_framebuffers());
    printf(" regs32x %016llx", (unsigned long long)hash_of(Pico32x.regs, sizeof(Pico32x.regs)));
    printf("\n");
}

static uint16_t vr_script_pad(long frame)
{
    uint16_t b = 0;
    for (int i = 0; i < vr_script_length; i++)
        if (frame >= vr_script_frames[i])
            b = vr_script_buttons[i];
    return b;
}
static void vr_panel_upload(void)
{
    static unsigned char rgba[224][320][4];
    static uint8_t hud_image[HLE_IMAGE_HEIGHT][HLE_IMAGE_WIDTH],
        hud_coverage[HLE_IMAGE_HEIGHT][HLE_IMAGE_WIDTH];
    memset(hud_image, 0, sizeof(hud_image));
    memset(hud_coverage, 0, sizeof(hud_coverage));
    const hle_frame_t *flat = hle_visible;
    if (vr_immersive && flat)
        for (int i = flat->hud_first; i < flat->count; i++) {
            sh2_draw_polygon(&hud_image[0][0], &flat->polygons[i], 0, 223);
            hle_polygon_t coverage = flat->polygons[i];
            coverage.colour = 0x100 | (coverage.colour & 3);
            sh2_draw_polygon(&hud_coverage[0][0], &coverage, 0, 223);
        }
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < 320; x++) {
            int row = core_visible_top + y;
            unsigned char *pixel = rgba[y][x];
            uint16_t c = core_screen[row * 320 + x];
            int visible = !vr_immersive || layers.x32_hud_item[row][x] || hud_coverage[y][x];
            if (vr_immersive && hud_coverage[y][x]) {
                uint16_t p = layers.x32_palette[row][hud_image[y][x]];
                pixel[0] = (p & 31) * 255 / 31;
                pixel[1] = ((p >> 5) & 31) * 255 / 31;
                pixel[2] = ((p >> 10) & 31) * 255 / 31;
            } else
                rgb565_to_rgb888(c, pixel);
            pixel[3] = visible ? 255 : 0;
            if (!visible)
                pixel[0] = pixel[1] = pixel[2] = 0;
        }
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, vr_panel_texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 320, 224, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glActiveTexture(GL_TEXTURE0);
}
static int vr_prepare(double time, double period, int active, const vr_input_t *input)
{
    double delta = vr_previous ? time - vr_previous : period;
    vr_previous = time;
    long freeze = vr_diagnostic ? (long)vr_env("VRDE_VR_FREEZE_FRAME", 0, 0, 100000) : 0;
    if (freeze && vr_sim_frames >= freeze)
        active = 0;
    if (!active) {
        vr_accumulator = 0;
        if (audio.device)
            SDL_ClearQueuedAudio(audio.device); /* Keep the last prepared scene while focus is elsewhere. */
        return vr_immersive;
    }
    vr_accumulator += fmax(0, fmin(delta, .1));
    int count = 0;
    while (vr_accumulator >= 1.0 / NTSC_FPS && count < 6) {
        uint16_t buttons = vr_diagnostic ? vr_script_pad(vr_sim_frames) : input->buttons;
        int steering = vr_diagnostic ? 0 : analog_stick_target(input->steer * 32767, stick_dead_zone);
        int throttle = vr_diagnostic ? 0 : analog_trigger_amount(input->throttle * 32767, trigger_dead_zone);
        int brake = vr_diagnostic ? 0 : analog_trigger_amount(input->brake * 32767, trigger_dead_zone);
        analog_set_input(steering, throttle, brake);
        run_frame(buttons);
        if (vr_diagnostic) {
            print_frame_hashes(vr_sim_frames);
            vr_hash(&vr_audio_hash, core_audio_samples, core_audio_frame_count * 4);
            vr_hash(&vr_video_hash, core_screen, sizeof(uint16_t) * CORE_SCREEN_WIDTH * CORE_SCREEN_HEIGHT);
        } else
            queue_audio();
        vr_sim_frames++;
        count++;
        vr_accumulator -= 1.0 / NTSC_FPS;
    }
    if (count)
        upload_frame();
    /* A race picture's original masks identify transitions and flat menus/results.
       Rebuild only while the game still submits fresh geometry, before the eyes. */
    vr_immersive = hle_visible && hle_visible->command == HLE_RACE_COMMAND && !in_transition &&
                   !(layers.x32_hud_types & ((1u << 7) | (1u << 8) | (1u << 10)));
    vr_scene = vr_immersive ? hle_vr_prepare(vr_accumulator * NTSC_FPS) : NULL;
    if (vr_scene && vr_scene->overflow) {
        fprintf(stderr, "[vr] mesh capacity exceeded\n");
        vr_immersive = 0;
        vr_scene = NULL;
    }
    if (vr_scene) {
        glBindBuffer(GL_ARRAY_BUFFER, vr_buffer);
        glBufferData(GL_ARRAY_BUFFER, vr_scene->count * sizeof(hle_vr_vertex_t), vr_scene->vertices,
                     GL_STREAM_DRAW);
    }
    vr_panel_upload();
    vr_display_frames++;
    return vr_immersive;
}
static void vr_dump_image(const char *name, int w, int h)
{
    unsigned char *pixels = malloc((size_t)w * h * 4);
    if (!pixels)
        return;
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface) {
        for (int y = 0; y < h; y++)
            memcpy((char *)surface->pixels + y * surface->pitch, pixels + (h - 1 - y) * (size_t)w * 4, w * 4);
        SDL_SaveBMP(surface, name);
        SDL_FreeSurface(surface);
    }
    free(pixels);
}
static void vr_eye(int eye, XrPosef pose, XrFovf fov, int w, int h)
{
    glViewport(0, 0, w, h);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glClearColor(.025f, .03f, .045f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (vr_immersive && vr_scene) {
        XrVector3f x = vr_rotate(pose.orientation, (XrVector3f){1, 0, 0}),
                   y = vr_rotate(pose.orientation, (XrVector3f){0, 1, 0}),
                   z = vr_rotate(pose.orientation, (XrVector3f){0, 0, 1});
        float rotation[16] = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, 0, 0, 0, 1};
        float bounds[16] = {tanf(fov.angleLeft), tanf(fov.angleRight), tanf(fov.angleDown),
                            tanf(fov.angleUp)};
        glUseProgram(vr_sky_program);
        int plane_size = Pico.video.reg[16] & 3;
        set_float(vr_sky_program, "sky_period", plane_size == 1 ? 512 : plane_size == 3 ? 1024 : 256);
        glUniformMatrix4fv(glGetUniformLocation(vr_sky_program, "rotation"), 1, GL_FALSE, rotation);
        glUniformMatrix4fv(glGetUniformLocation(vr_sky_program, "bounds"), 1, GL_FALSE, bounds);
        glBindVertexArray(quad_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        float matrix[16];
        float level = vr_env("VRDE_VR_HORIZON_LOCK", 1, 0, 1);
        vr_matrix(matrix, pose, fov, vr_env("VRDE_VR_WORLD_SCALE", 16, 4, 64), level * vr_scene->native_pitch,
                  level * vr_scene->native_roll);
        glUseProgram(vr_program);
        glUniformMatrix4fv(glGetUniformLocation(vr_program, "matrix"), 1, GL_FALSE, matrix);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glBindVertexArray(vr_vao);
        glDrawArrays(GL_TRIANGLES, 0, vr_scene->count);
        glDisable(GL_DEPTH_TEST);
    }
}
static void vr_eye_resolved(int eye, XrPosef pose, int w, int h)
{
    int target = (int)vr_env("VRDE_VR_DUMP_FRAME", 0, 0, 1000000);
    if (target > 0 && vr_sim_frames >= target && !(vr_dumped & (1 << eye))) {
        char name[64];
        snprintf(name, sizeof(name), "vr-eye%d.bmp", eye);
        vr_dump_image(name, w, h);
        vr_dumped |= 1 << eye;
        printf("[vr] dump eye=%d sim=%ld display=%ld vertices=%d position=%.5f,%.5f,%.5f\n", eye,
               vr_sim_frames, vr_display_frames, vr_scene ? vr_scene->count : 0, pose.position.x,
               pose.position.y, pose.position.z);
    }
}
static void vr_panel(int w, int h, int immersive)
{
    (void)immersive;
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(vr_panel_program);
    glBindVertexArray(quad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if ((vr_dumped & 3) == 3 && !(vr_dumped & 4)) {
        vr_dump_image("vr-hud.bmp", w, h);
        vr_dumped |= 4;
    }
}
static int vr_host_diagnostic(int frames, int *script_frames, uint16_t *script_buttons, int script_length)
{
    vr_diagnostic = 1;
    vr_script_frames = script_frames;
    vr_script_buttons = script_buttons;
    vr_script_length = script_length;
    vr_input_t input = {0};
    double time = 0;
    double hz = vr_env("VRDE_VR_DIAG_HZ", 90, 60, 144);
    /* Exercise the actual eye callbacks on every display tick, including ticks
       with no emulation step. This is a state/cadence check, not a GPU benchmark. */
    GLuint cadence_fbo = create_offscreen_framebuffer(256, 256), cadence_depth;
    glGenRenderbuffers(1, &cadence_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, cadence_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 256, 256);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, cadence_depth);
    XrFovf cadence_fov = {-.785398f, .785398f, .785398f, -.785398f};
    while (vr_sim_frames < frames) {
        time += 1 / hz;
        vr_prepare(time, 1 / hz, 1, &input);
        for (int eye = 0; eye < 2; eye++) {
            XrPosef pose = {{0, 0, 0, 1}, {eye ? .032f : -.032f, 0, 0}};
            glBindFramebuffer(GL_FRAMEBUFFER, cadence_fbo);
            vr_eye(eye, pose, cadence_fov, 256, 256);
            vr_eye_resolved(eye, pose, 256, 256);
        }
        vr_panel(256, 256, vr_immersive);
    }
    /* Independently reconstruct camera coordinates from the existing exact flat
       projection and verify every un-clipped corner exists in the source mesh. */
    const hle_frame_t *identity = hle_interpolated(vr_accumulator * NTSC_FPS, hle_visible, 0);
    int identity_checked = 0, identity_missing = 0;
    float worst = 0;
    if (identity && vr_scene)
        for (int i = 0; i < identity->hud_first; i++) {
            const hle_polygon_t *p = &identity->polygons[i];
            for (int k = 0; k < p->vertex_count; k++) {
                float z = p->depth[k];
                if (z < 32)
                    continue; /* clipped near-plane intersections */
                float x = p->fx[k] * z / 112, y = p->fy[k] * z / 112, best = 1e30f;
                for (int j = 0; j < vr_scene->count; j++) {
                    const hle_vr_vertex_t *v = &vr_scene->vertices[j];
                    float error = fmaxf(fabsf(v->x - x), fmaxf(fabsf(v->y - y), fabsf(v->z - z)));
                    best = fminf(best, error);
                }
                identity_checked++;
                worst = fmaxf(worst, best);
                if (best > .01f)
                    identity_missing++;
            }
        }
    printf("[vr-diag] identity checked=%d missing=%d worst_world_error=%.6f\n", identity_checked,
           identity_missing, worst);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLuint fbo = create_offscreen_framebuffer(768, 768), depth;
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 768, 768);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    XrFovf fov = {-.785398f, .785398f, .785398f, -.785398f};
    for (int yaw = 0; yaw <= 180; yaw += 90) {
        XrQuaternionf q = vr_yaw(yaw * M_PI / 180);
        for (int eye = 0; eye < 2; eye++) {
            XrPosef pose = {q, vr_rotate(q, (XrVector3f){eye ? .032f : -.032f, 0, 0})};
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            vr_eye(eye, pose, fov, 768, 768);
            vr_eye_resolved(eye, pose, 768, 768);
            char name[64];
            snprintf(name, sizeof(name), "vr-yaw%d-eye%d.bmp", yaw, eye);
            vr_dump_image(name, 768, 768);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    vr_panel(768, 768, vr_immersive);
    vr_dump_image("vr-hud.bmp", 768, 768);
    printf("[vr-diag] sim=%ld display=%ld scene=%d vertices=%d overflow=%d video=%016llx audio=%016llx "
           "hz=%.1f\n",
           vr_sim_frames, vr_display_frames, vr_scene ? vr_scene->scene : 0, vr_scene ? vr_scene->count : 0,
           vr_scene ? vr_scene->overflow : 0, vr_video_hash, vr_audio_hash, hz);
    if (vr_scene) {
        printf("[vr-diag] skids stored=%u total=%u drawn=%d cars=",vr_scene->skid_count,
               vr_scene->skid_total,(vr_scene->count-vr_scene->skid_first)/3);
        for(int car=0;car<16;car++)printf("%s%u",car?",":"",vr_scene->skid_cars[car]);
        puts("");
    }
    if (vr_scene)
        printf("[vr-diag] car_body_length=%.5f native_pitch=%.5f native_roll=%.5f\n", vr_scene->car_length,
               vr_scene->native_pitch, vr_scene->native_roll);
    return vr_immersive && vr_scene && vr_scene->count > 0 && !vr_scene->overflow && !identity_missing &&
                   identity_checked > 0
               ? 0
               : 1;
}

static long vr_simulation_frames(void) { return vr_sim_frames; }
