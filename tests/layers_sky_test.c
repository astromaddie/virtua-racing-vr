/* Compare the production tile-span panorama against the original per-pixel
   formula. Random VRAM exercises flips, palette banks and transparent pixels. */
#include "../src/hud.c"
#include "../src/layers.c"
#include <assert.h>
#include <time.h>

struct Pico Pico;
struct PicoMem PicoMem;
struct Pico32x Pico32x;
struct Pico32xMem *Pico32xMem;
SH2 sh2s[2];
uint16_t core_screen[CORE_SCREEN_WIDTH * CORE_SCREEN_HEIGHT];
int core_visible_top;
void (*vrde_32x_layer_hook)(int,int,int,int);
static uint16_t reference[LAYERS_HEIGHT][SKY_WIDTH];

static void draw_reference(void)
{
    const unsigned char *reg = Pico.video.reg;
    int nametable = (reg[4] & 7) << 13;
    int width = plane_cells(reg[16] & 3), height = plane_cells((reg[16] >> 4) & 3);
    int hscroll = layers_plane_b_scroll(), vscroll = PicoMem.vsram[1] & 0x3ff;
    int background = reg[7] & 0x3f;
    for (int row = 0; row < LAYERS_HEIGHT; row++) {
        int py = (row - core_visible_top + vscroll) & (height * 8 - 1);
        for (int i = 0; i < SKY_WIDTH; i++) {
            int px = (i - SKY_MARGIN - hscroll) & (width * 8 - 1);
            uint16_t entry = PicoMem.vram[(nametable + ((py / 8) * width + px / 8) * 2) / 2];
            int tx = entry & 0x0800 ? 7 - px % 8 : px % 8;
            int ty = entry & 0x1000 ? 7 - py % 8 : py % 8;
            uint16_t word = PicoMem.vram[((entry & 0x7ff) * 32 + ty * 4 + (tx / 4) * 2) / 2];
            int pixel = (word >> (12 - 4 * (tx % 4))) & 15;
            reference[row][i] = Pico.est.HighPal[pixel ? ((entry >> 13) & 3) * 16 + pixel : background];
        }
    }
}

int main(int argc, char **argv)
{
    static const unsigned scroll[] = {0,1,7,8,255,511,512,1023};
    unsigned rng = 0x12345678, cases = 0;
    double old_time = 0, new_time = 0;
    for (unsigned w = 0; w < 4; w++) for (unsigned h = 0; h < 4; h++)
    for (unsigned s = 0; s < sizeof(scroll)/sizeof(scroll[0]); s++) {
        for (unsigned i = 0; i < sizeof(PicoMem.vram)/sizeof(PicoMem.vram[0]); i++) {
            rng = rng * 1664525u + 1013904223u;
            PicoMem.vram[i] = rng >> 16;
        }
        for (unsigned i = 0; i < 64; i++) Pico.est.HighPal[i] = (i * 971u) ^ (s * 31u);
        Pico.video.reg[4] = s & 3;
        Pico.video.reg[7] = (s * 11) & 63;
        Pico.video.reg[13] = 0x3f;
        Pico.video.reg[16] = w | (h << 4);
        PicoMem.vram[(0xfc00 + 2) / 2] = scroll[s];
        PicoMem.vsram[1] = scroll[7-s];
        core_visible_top = (s % 4) * 8 - 8;
        clock_t start = clock();
        draw_reference();
        old_time += (double)(clock()-start)/CLOCKS_PER_SEC;
        memset(row_captured, 1, sizeof(row_captured));
        start = clock();
        layers_frame_start();
        new_time += (double)(clock()-start)/CLOCKS_PER_SEC;
        assert(!memcmp(reference, layers.md_wide, sizeof(reference)));
        for (unsigned i = 0; i < sizeof(row_captured); i++) assert(!row_captured[i]);
        assert(layers.md_wide_scroll == (int)scroll[s]);
        assert(layers.md_wide_lines == plane_cells(h)*8);
        assert(layers.md_wide_first_line == (-(int)scroll[7-s] & (plane_cells(h)*8-1)));
        cases++;
    }
    printf("Sky panorama: %u cases, exact pixels and metadata across sizes/scroll/wrap/flips/palettes\n", cases);
    if (argc > 1 && !strcmp(argv[1], "--bench"))
        printf("Sky CPU time (same inputs): reference %.6fs, tile spans %.6fs\n", old_time, new_time);
}
