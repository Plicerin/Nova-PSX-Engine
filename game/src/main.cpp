// Demo game entry point (spec 14 M8): fixed 60 Hz sim, render every loop.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/core/types.h"
#include "engine/core/config.h"
#include "engine/math/fixed.h"
#include "engine/platform/platform.h"
#include "engine/input/input.h"
#include "engine/audio/audio.h"
#include "engine/assets/assets.h"
#include "engine/renderer/vram.h"
#include "engine/renderer/framebuffer.h"
#include "engine/renderer/render.h"
#include "engine/debug/debug.h"
#include "game/src/combat.h"
#include "game/src/demo_scene.h"

// Framebuffer ~1.2 MB, RenderContext ~4 MB: must be static, never stack.
static Framebuffer   g_fb;
static RenderContext g_rc;

static constexpr int kMaxShots = 64;
static int s_shots[kMaxShots];
static int s_shot_count = 0;
static i64 s_max_frames = -1;   // <0 = run until quit
static int s_debug_page  = 0;   // --debug N: start with overlay page N
static const char* s_level_path = "build/assets/levels/test_chamber.lvlbin";
static int  s_resmode = -1;     // --resmode N: override internal resolution index
static int  s_fov     = -1;     // --fov N: override horizontal FOV
static bool s_noui    = false;  // --noui: skip title/help overlay
static i32  s_spin    = 0;      // --spin N: turntable deg/tick for the character
static i64  s_freecam_at = -1;  // --freecam-at N: toggle freecam at frame N (test)
static const char* s_clip = nullptr;  // --clip NAME [--clip-at N]: play robot clip
static i64  s_clip_at = 0;
static i64  s_inspect_at = -1;  // --inspect-at N: toggle status screen (test)

static void AudioCallback(i16* out_stereo, int frames, void* user) {
    (void)user;
    Audio_Mix(out_stereo, frames);
}

static bool ParseShots(const char* arg) {
    const char* p = arg;
    while (*p && s_shot_count < kMaxShots) {
        char* end = nullptr;
        long v = strtol(p, &end, 10);
        if (end == p) return false;
        s_shots[s_shot_count++] = (int)v;
        p = end;
        if (*p != ',') break;
        p++;
    }
    return true;
}

static bool ShotAtFrame(i64 frame) {
    for (int i = 0; i < s_shot_count; i++)
        if ((i64)s_shots[i] == frame) return true;
    return false;
}

int main(int argc, char** argv) {
    int win_w = 960, win_h = 720;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            s_max_frames = (i64)strtoll(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--shots") == 0 && i + 1 < argc) {
            if (!ParseShots(argv[++i]))
                fprintf(stderr, "main: bad --shots list '%s'\n", argv[i]);
        } else if (strcmp(argv[i], "--debug") == 0 && i + 1 < argc) {
            s_debug_page = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--zbuffer") == 0) {
            g_config.zbuffer = true;   // ground-truth depth (debug comparison)
        } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            s_level_path = argv[++i];
        } else if (strcmp(argv[i], "--resmode") == 0 && i + 1 < argc) {
            s_resmode = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fov") == 0 && i + 1 < argc) {
            s_fov = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--spin") == 0 && i + 1 < argc) {
            s_spin = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--freecam-at") == 0 && i + 1 < argc) {
            s_freecam_at = (i64)strtoll(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--clip") == 0 && i + 1 < argc) {
            s_clip = argv[++i];
        } else if (strcmp(argv[i], "--clip-at") == 0 && i + 1 < argc) {
            s_clip_at = (i64)strtoll(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--inspect-at") == 0 && i + 1 < argc) {
            s_inspect_at = (i64)strtoll(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--combat") == 0) {
            Combat_SetActive(true);
        } else if (strcmp(argv[i], "--combat-auto") == 0) {
            Combat_SetActive(true);
            Combat_SetAuto(true);
        } else if (strcmp(argv[i], "--noui") == 0) {
            s_noui = true;
        } else if (strcmp(argv[i], "--wire") == 0) {
            g_config.wireframe = true;
        } else if (strcmp(argv[i], "--bloom") == 0) {
            g_config.bloom = true;
        } else if (strcmp(argv[i], "--spec") == 0) {
            g_specular.enabled = true;
        } else if (strcmp(argv[i], "--res") == 0 && i + 2 < argc) {
            win_w = atoi(argv[i + 1]);
            win_h = atoi(argv[i + 2]);
            i += 2;
            if (win_w < 64 || win_h < 64) {
                fprintf(stderr, "main: bad --res, using 960x720\n");
                win_w = 960; win_h = 720;
            }
        } else {
            fprintf(stderr, "main: unknown argument '%s'\n", argv[i]);
        }
    }

    Fixed_Init();
    if (!Plat_Init("PSX-Authentic Engine", win_w, win_h)) {
        fprintf(stderr, "main: platform init failed\n");
        return 1;
    }
    Audio_Init();
    if (!Plat_StartAudio(AUDIO_RATE, AudioCallback, nullptr))
        fprintf(stderr, "main: audio device unavailable, continuing silent\n");
    Input_Init();
    Debug_Init();
    Debug_SetOverlayPage(s_debug_page);
    Vram_Clear();
    if (!Assets_LoadAll("build/assets/manifest.bin")) {
        fprintf(stderr, "main: asset load failed\n");
        Plat_Shutdown();
        return 1;
    }
    Level* level = Level_Load(s_level_path);
    if (!level) {
        fprintf(stderr, "main: level load failed (%s)\n", s_level_path);
        Plat_Shutdown();
        return 1;
    }
    if (s_resmode >= 0 && s_resmode < kResModeCount) g_config.res_mode = s_resmode;
    if (s_fov > 0) g_config.fov_deg = s_fov;
    Rc_Init(&g_rc);
    Demo_SetSpin(s_spin);
    Demo_Init(level);

    Fb_Init(&g_fb, g_config.InternalW(), g_config.InternalH());
    int last_res_mode = g_config.res_mode;

    constexpr u64 kStepNS = 16666667ull;
    u64 sim_prev   = Plat_TicksNS();
    u64 acc        = 0;
    u64 frame_prev = sim_prev;
    u64 fps_start  = sim_prev;
    int fps_frames = 0;
    i32 fps_x10 = 0, frame_ms_x10 = 0;
    i64 frame_index = 0;

    for (;;) {
        u64 t0 = Plat_TicksNS();
        i32 dt_ms = ClampI32((i32)((t0 - frame_prev) / 1000000ull), 1, 100);
        frame_prev = t0;

        if (!Plat_PumpEvents()) break;
        Input_Update();
        Debug_Update(&g_rc, dt_ms);

        if (g_config.res_mode != last_res_mode) {
            last_res_mode = g_config.res_mode;
            Fb_Init(&g_fb, g_config.InternalW(), g_config.InternalH());
        }

        u64 now = Plat_TicksNS();
        acc += now - sim_prev;
        sim_prev = now;
        if (acc > 4 * kStepNS) acc = 4 * kStepNS;   // clamp backlog: never spiral
        if (Debug_Paused()) {
            acc = 0;
            if (Debug_StepFrame()) Demo_Update();
        } else {
            while (acc >= kStepNS) {
                Demo_Update();
                Input_ConsumeEdges();   // edges are one-tick events
                acc -= kStepNS;
            }
        }

        if (frame_index == s_freecam_at) Debug_ToggleFreeCam(&g_rc);
        if (frame_index == s_inspect_at) Combat_ToggleInspect();
        if (s_clip && frame_index == s_clip_at) Demo_PlayClip(s_clip);
        if (ShotAtFrame(frame_index)) Debug_RequestScreenshot();

        Demo_Render(&g_rc, &g_fb);
        if (g_config.bloom) Fb_Bloom(&g_fb, g_config.bloom_threshold, g_config.bloom_strength);
        if (!s_noui) Demo_DrawUI(&g_fb);
        Debug_DrawOverlay(&g_fb, &g_rc, fps_x10, frame_ms_x10);
        if (Debug_ScreenshotPending()) Debug_TakeScreenshot(&g_fb);

        frame_ms_x10 = (i32)((Plat_TicksNS() - t0) / 100000ull); // pump -> present
        Plat_Present(g_fb.pixels, g_fb.w, g_fb.h);

        fps_frames++;
        u64 t1 = Plat_TicksNS();
        if (t1 - fps_start >= 1000000000ull) {
            fps_x10 = (i32)(((u64)fps_frames * 10000000000ull) / (t1 - fps_start));
            fps_frames = 0;
            fps_start = t1;
        }

        frame_index++;
        if (s_max_frames >= 0 && frame_index >= s_max_frames) break;
    }

    Plat_Shutdown();
    return 0;
}
