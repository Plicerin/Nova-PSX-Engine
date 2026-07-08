// Debug & developer tools (spec 13): overlay, toggles, free camera,
// pause/step, screenshots, bitmap text.
#include "engine/debug/debug.h"
#include "engine/debug/font8x8_basic.h"
#include "engine/core/config.h"
#include "engine/math/fixed.h"
#include "engine/renderer/raster.h"
#include "engine/renderer/prim.h"
#include "engine/renderer/vram.h"
#include "engine/platform/platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ------------------------------------------------------------------ state

namespace {

struct DebugState {
    int    page;          // overlay page 0..3
    bool   paused;
    bool   step;          // one-shot, armed by O while paused
    bool   freecam_on;
    bool   synced;        // Debug_SyncFreeCam was called at least once
    bool   shot_pending;
    int    shot_counter;  // NNN in screenshot filenames
    Camera freecam;
    Camera synced_cam;    // last camera passed to Debug_SyncFreeCam
};

DebugState s_dbg;

} // namespace

void Debug_Init() { memset(&s_dbg, 0, sizeof(s_dbg)); }

void Debug_SetOverlayPage(int page) {
    if (page < 0) page = 0;
    if (page > 3) page = 3;
    s_dbg.page = page;
}

// ------------------------------------------------------------------ text

static int DrawString(Framebuffer* fb, int x, int y, int r, int g, int b,
                      const char* str) {
    const int start = x;
    for (const char* c = str; *c; c++) {
        unsigned ch = (unsigned char)*c;
        if (ch < 32 || ch > 126) ch = 32;
        for (int row = 0; row < 8; row++) {
            const u8 bits = (u8)font8x8_basic[ch][row];
            if (!bits) continue;
            const int py = y + row;
            if (py < 0 || py >= fb->h) continue;
            for (int col = 0; col < 8; col++) {
                if (!((bits >> col) & 1)) continue; // LSB = leftmost pixel
                const int px = x + col;
                if (px < 0 || px >= fb->w) continue;
                Fb_Put(fb, px, py, r, g, b);
            }
        }
        x += 8;
    }
    return x - start;
}

int Debug_Text(Framebuffer* fb, int x, int y, u8 r, u8 g, u8 b,
               const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DrawString(fb, x + 1, y + 1, 16, 16, 16, buf); // drop shadow for readability
    return DrawString(fb, x, y, r, g, b, buf);
}

// ------------------------------------------------------------------ update

static void FreeCamFly(i32 dt_ms) {
    Camera* cam = &s_dbg.freecam;
    i32 spd = (6 * WORLD_SCALE * dt_ms) / 1000; // ~6 m/s
    if (spd < 1) spd = 1;
    i32 rspd = (400 * dt_ms) / 1000;            // ~400 angle units/s
    if (rspd < 1) rspd = 1;

    i32 yaw = cam->rot.vy, pitch = cam->rot.vx;
    if (Plat_KeyHeld(PK_LEFT))  yaw -= rspd;
    if (Plat_KeyHeld(PK_RIGHT)) yaw += rspd;
    if (Plat_KeyHeld(PK_UP))    pitch -= rspd;
    if (Plat_KeyHeld(PK_DOWN))  pitch += rspd;
    yaw &= ANGLE_FULL - 1;
    pitch = ClampI32(pitch, -1000, 1000); // stop short of straight up/down
    cam->rot.vy = (i16)yaw;
    cam->rot.vx = (i16)pitch;

    // Move in the yaw plane; y grows DOWN so "up" is -y.
    const fx12 sy = Csin(yaw), cy = Ccos(yaw);
    const i32 fwd_x = (sy * spd) >> FX_SHIFT, fwd_z = (cy * spd) >> FX_SHIFT;
    const i32 rgt_x = (cy * spd) >> FX_SHIFT, rgt_z = (-sy * spd) >> FX_SHIFT;

    if (Plat_KeyHeld(PK_W)) { cam->pos.vx += fwd_x; cam->pos.vz += fwd_z; }
    if (Plat_KeyHeld(PK_S)) { cam->pos.vx -= fwd_x; cam->pos.vz -= fwd_z; }
    if (Plat_KeyHeld(PK_D)) { cam->pos.vx += rgt_x; cam->pos.vz += rgt_z; }
    if (Plat_KeyHeld(PK_A)) { cam->pos.vx -= rgt_x; cam->pos.vz -= rgt_z; }
    if (Plat_KeyHeld(PK_R) || Plat_KeyHeld(PK_Q)) cam->pos.vy -= spd;
    if (Plat_KeyHeld(PK_F) || Plat_KeyHeld(PK_E)) cam->pos.vy += spd;
}

void Debug_Update(RenderContext* rc, i32 dt_ms) {
    if (Plat_KeyPressed(PK_F1))  s_dbg.page = (s_dbg.page + 1) & 3;
    if (Plat_KeyPressed(PK_F2))  g_config.wireframe = !g_config.wireframe;
    if (Plat_KeyPressed(PK_F3))  g_config.dithering = !g_config.dithering;
    if (Plat_KeyPressed(PK_F4))  g_config.color_quantization = !g_config.color_quantization;
    if (Plat_KeyPressed(PK_F5))  g_config.perspective_correct = !g_config.perspective_correct;
    if (Plat_KeyPressed(PK_F6))  g_config.zbuffer = !g_config.zbuffer;
    if (Plat_KeyPressed(PK_F7))  g_config.vertex_snapping = !g_config.vertex_snapping;
    if (Plat_KeyPressed(PK_F8))  g_config.bilinear_filter = !g_config.bilinear_filter;
    if (Plat_KeyPressed(PK_F9))  g_debug_draworder = !g_debug_draworder;
    if (Plat_KeyPressed(PK_F10)) g_config.res_mode = (g_config.res_mode + 1) % kResModeCount; // main loop reacts
    if (Plat_KeyPressed(PK_F11)) g_config.scale_mode = (ScaleMode)((g_config.scale_mode + 1) % SCALE_MODE_COUNT);
    if (Plat_KeyPressed(PK_F12)) Debug_RequestScreenshot();

    if (Plat_KeyPressed(PK_P)) s_dbg.paused = !s_dbg.paused;
    if (Plat_KeyPressed(PK_O) && s_dbg.paused) s_dbg.step = true;

    if (Plat_KeyPressed(PK_TAB)) {
        s_dbg.freecam_on = !s_dbg.freecam_on;
        if (s_dbg.freecam_on) {
            if (s_dbg.synced)  s_dbg.freecam = s_dbg.synced_cam;
            else if (rc)       s_dbg.freecam = rc->cam; // main never synced yet
        }
    }

    if (s_dbg.freecam_on) FreeCamFly(dt_ms);
}

bool Debug_Paused() { return s_dbg.paused; }

bool Debug_StepFrame() {
    if (!s_dbg.step) return false;
    s_dbg.step = false;
    return true;
}

bool Debug_FreeCamActive() { return s_dbg.freecam_on; }

const Camera* Debug_FreeCam() { return &s_dbg.freecam; }

void Debug_SyncFreeCam(const Camera* scene_cam) {
    if (!scene_cam) return;
    s_dbg.synced_cam = *scene_cam;
    s_dbg.synced = true;
    if (!s_dbg.freecam_on) s_dbg.freecam = *scene_cam;
}

// ------------------------------------------------------------------ overlay

static void FillRect(Framebuffer* fb, int x, int y, int w, int h,
                     int r, int g, int b) {
    const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    const int x1 = x + w > fb->w ? fb->w : x + w;
    const int y1 = y + h > fb->h ? fb->h : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            Fb_Put(fb, xx, yy, r, g, b);
}

static void RectOutline(Framebuffer* fb, int x, int y, int w, int h,
                        u8 r, u8 g, u8 b) {
    Raster_LinePx(fb, x, y, x + w - 1, y, r, g, b);
    Raster_LinePx(fb, x, y + h - 1, x + w - 1, y + h - 1, r, g, b);
    Raster_LinePx(fb, x, y, x, y + h - 1, r, g, b);
    Raster_LinePx(fb, x + w - 1, y, x + w - 1, y + h - 1, r, g, b);
}

// 4096 OT buckets -> 128 columns (max over each 32), log2-ish bar height.
static void DrawOtHistogram(Framebuffer* fb, const RenderContext* rc) {
    const int px0 = 2;
    const int base_y = fb->h - 3;
    Debug_Text(fb, px0, base_y - 32 - 10, 180, 180, 180, "OT");
    Raster_LinePx(fb, px0, base_y + 1, px0 + 127, base_y + 1, 90, 90, 90);
    for (int col = 0; col < 128; col++) {
        u32 maxn = 0;
        for (int k = 0; k < 32; k++) {
            u32 n = 0;
            for (const Prim* p = rc->ot.bucket[col * 32 + k];
                 p && n < (u32)PRIM_ARENA_MAX; p = p->next) n++;
            if (n > maxn) maxn = n;
        }
        if (!maxn) continue;
        int hgt = 4; // 1 prim = 4px, +4px per doubling, cap 32
        for (u32 v = maxn; v > 1 && hgt < 32; v >>= 1) hgt += 4;
        Raster_LinePx(fb, px0 + col, base_y, px0 + col, base_y - (hgt - 1),
                      96, 224, 128);
    }
}

// 16x2 page grid, brightness = occupancy; red border = notional display area.
static void DrawVramMap(Framebuffer* fb) {
    const int cell = 6;
    const int gw = 16 * cell, gh = 2 * cell;
    const int gx = fb->w - gw - 2;
    const int gy = fb->h - gh - 9; // room for budget bar below
    Debug_Text(fb, gx, gy - 10, 180, 180, 180, "VRAM");
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 16; col++) {
            const int cx = gx + col * cell, cy = gy + row * cell;
            const int occ = Vram_PageOccupancy(col, row);
            const int lum = 20 + (occ * 235) / 255;
            FillRect(fb, cx + 1, cy + 1, cell - 2, cell - 2, lum, lum, lum);
            if (col < VRAM_TEX_X0 / 64) // cols 0..4: reserved display buffers
                RectOutline(fb, cx, cy, cell, cell, 220, 40, 40);
            else
                RectOutline(fb, cx, cy, cell, cell, 70, 70, 70);
        }
    }
    const int by = gy + gh + 2;
    RectOutline(fb, gx, by, gw, 5, 110, 110, 110);
    const u32 used = Vram_UsedBytes();
    int fill = (int)(((u64)used * (u32)(gw - 2)) / VRAM_TEX_BUDGET);
    if (fill > gw - 2) fill = gw - 2;
    if (fill > 0) FillRect(fb, gx + 1, by + 1, fill, 3, 230, 190, 70);
}

void Debug_DrawOverlay(Framebuffer* fb, const RenderContext* rc,
                       i32 fps_x10, i32 frame_ms_x10) {
    if (!fb || !rc || s_dbg.page == 0) return;

    const u8 tr = 200, tg = 230, tb = 200;
    int y = 2;
    Debug_Text(fb, 2, y, tr, tg, tb, "FPS %d.%d", fps_x10 / 10, fps_x10 % 10);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "FRAME %d.%dMS",
               frame_ms_x10 / 10, frame_ms_x10 % 10);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "PRIMS %u/%u",
               rc->stats.prims_emitted, g_raster_stats.prims_drawn);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "TRIS %u/%u",
               rc->stats.tris_emitted, g_raster_stats.tris_drawn);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "CULLED %u", rc->stats.faces_culled);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "ARENA %d/%d", rc->arena.used, PRIM_ARENA_MAX);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "TEXMEM %uKB/%uKB",
               Vram_UsedBytes() / 1024u, VRAM_TEX_BUDGET / 1024u);
    y += 9;
    int pages = 0;
    for (int prow = 0; prow < 2; prow++)
        for (int pcol = 0; pcol < 16; pcol++)
            if (Vram_PageOccupancy(pcol, prow) > 0) pages++;
    Debug_Text(fb, 2, y, tr, tg, tb, "PAGES %d/32", pages);
    y += 9;
    Debug_Text(fb, 2, y, tr, tg, tb, "RES %dx%d", fb->w, fb->h);
    y += 9;
    Debug_Text(fb, 2, y, 180, 200, 255, "[%s][%s][%s][%s][%s]%s%s%s",
               g_config.perspective_correct ? "PERSP" : "AFF",
               g_config.dithering ? "DITH" : "NODITH",
               g_config.color_quantization ? "QNT" : "24BIT",
               g_config.vertex_snapping ? "SNAP" : "SUBPX",
               g_config.zbuffer ? "ZBUF" : "PAINT",
               g_config.bilinear_filter ? "[BILIN]" : "",
               g_config.wireframe ? "[WIRE]" : "",
               g_debug_draworder ? "[ORDER]" : "");
    y += 9;
    int ix = 2;
    if (s_dbg.freecam_on)
        ix += Debug_Text(fb, ix, y, 255, 220, 96, "FREECAM ");
    if (s_dbg.paused)
        Debug_Text(fb, ix, y, 255, 96, 96, "PAUSED");

    if (s_dbg.page == 2) DrawOtHistogram(fb, rc);
    if (s_dbg.page == 3) DrawVramMap(fb);
}

// ------------------------------------------------------------------ screenshots

void Debug_RequestScreenshot() { s_dbg.shot_pending = true; }

bool Debug_ScreenshotPending() { return s_dbg.shot_pending; }

void Debug_TakeScreenshot(const Framebuffer* fb) {
    s_dbg.shot_pending = false;
    if (!fb) return;
    Plat_MkDir("build");
    Plat_MkDir("build/bin");
    Plat_MkDir("build/bin/screenshots");
    char path[96];
    snprintf(path, sizeof(path), "build/bin/screenshots/raw_%03d.bmp",
             s_dbg.shot_counter);
    bool ok = Debug_WriteBMP(path, fb->pixels, fb->w, fb->h, 1);
    snprintf(path, sizeof(path), "build/bin/screenshots/up_%03d.bmp",
             s_dbg.shot_counter);
    ok = Debug_WriteBMP(path, fb->pixels, fb->w, fb->h, 3) && ok;
    if (!ok)
        fprintf(stderr, "[debug] screenshot %03d failed\n", s_dbg.shot_counter);
    s_dbg.shot_counter++;
}

static void Put16(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8);
}
static void Put32(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

bool Debug_WriteBMP(const char* path, const u32* xrgb, int w, int h, int scale_n) {
    if (!path || !xrgb || w <= 0 || h <= 0 || scale_n < 1) {
        fprintf(stderr, "[debug] Debug_WriteBMP: bad args\n");
        return false;
    }
    const int ow = w * scale_n, oh = h * scale_n;
    const u32 row_bytes = ((u32)ow * 3u + 3u) & ~3u; // rows padded to 4 bytes
    const u32 img_size = row_bytes * (u32)oh;
    const u32 file_size = 54u + img_size;
    u8* buf = (u8*)malloc(file_size);
    if (!buf) {
        fprintf(stderr, "[debug] Debug_WriteBMP: malloc %u failed\n", file_size);
        return false;
    }
    memset(buf, 0, 54);
    buf[0] = 'B'; buf[1] = 'M';
    Put32(buf + 2, file_size);
    Put32(buf + 10, 54);       // pixel data offset
    Put32(buf + 14, 40);       // BITMAPINFOHEADER size
    Put32(buf + 18, (u32)ow);
    Put32(buf + 22, (u32)oh);  // positive height = bottom-up
    Put16(buf + 26, 1);        // planes
    Put16(buf + 28, 24);       // bits per pixel
    Put32(buf + 30, 0);        // BI_RGB
    Put32(buf + 34, img_size);
    Put32(buf + 38, 2835);     // 72 dpi
    Put32(buf + 42, 2835);
    for (int oy = 0; oy < oh; oy++) {
        const int sy = (oh - 1 - oy) / scale_n; // bottom-up row order
        const u32* src = xrgb + (size_t)sy * (size_t)w;
        u8* row = buf + 54 + (size_t)oy * row_bytes;
        for (int ox = 0; ox < ow; ox++) {
            const u32 c = src[ox / scale_n];    // nearest-neighbor duplication
            row[ox * 3 + 0] = (u8)(c);          // B
            row[ox * 3 + 1] = (u8)(c >> 8);     // G
            row[ox * 3 + 2] = (u8)(c >> 16);    // R
        }
        for (u32 pad = (u32)ow * 3u; pad < row_bytes; pad++) row[pad] = 0;
    }
    const bool ok = Plat_WriteFile(path, buf, file_size);
    free(buf);
    if (!ok)
        fprintf(stderr, "[debug] Debug_WriteBMP: write '%s' failed\n", path);
    return ok;
}
