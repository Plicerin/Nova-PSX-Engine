// SDL3 platform layer. The ONLY translation unit allowed to include SDL.
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <direct.h>

#include "engine/core/types.h"
#include "engine/core/config.h"
#include "engine/platform/platform.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static SDL_Window*   g_win = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture*  g_tex = nullptr;
static SDL_Gamepad*  g_pad = nullptr;

static bool g_held[PK_COUNT];
static bool g_pressed[PK_COUNT];
static GamepadState g_pad_state;

static SDL_AudioStream*  g_stream     = nullptr;
static PlatAudioCallback g_audio_cb   = nullptr;
static void*             g_audio_user = nullptr;

// PlatKey -> SDL scancode. Explicit because SDL digit order is 1..9,0.
static const SDL_Scancode kKeyToScan[PK_COUNT] = {
    SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D,
    SDL_SCANCODE_E, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
    SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P,
    SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
    SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
    SDL_SCANCODE_Y, SDL_SCANCODE_Z,
    SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
    SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7,
    SDL_SCANCODE_8, SDL_SCANCODE_9,
    SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
    SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
    SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE,
    SDL_SCANCODE_TAB, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL,
    SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_MINUS,
    SDL_SCANCODE_EQUALS,
};

// Reverse lookup: scancode -> PlatKey (0xFF = unmapped).
static u8 g_scan_to_key[512];

static void BuildScanTable() {
    memset(g_scan_to_key, 0xFF, sizeof(g_scan_to_key));
    for (int k = 0; k < PK_COUNT; k++) {
        int sc = (int)kKeyToScan[k];
        if (sc >= 0 && sc < (int)(sizeof(g_scan_to_key)))
            g_scan_to_key[sc] = (u8)k;
    }
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------
bool Plat_Init(const char* title, int win_w, int win_h) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        fprintf(stderr, "Plat_Init: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    g_win = SDL_CreateWindow(title, win_w, win_h, SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        fprintf(stderr, "Plat_Init: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    g_ren = SDL_CreateRenderer(g_win, NULL);
    if (!g_ren) {
        fprintf(stderr, "Plat_Init: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win); g_win = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_SetRenderVSync(g_ren, 1);

    // One max-size streaming texture; smaller internal modes upload a subrect.
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_XRGB8888,
                              SDL_TEXTUREACCESS_STREAMING, FB_MAX_W, FB_MAX_H);
    if (!g_tex) {
        fprintf(stderr, "Plat_Init: SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(g_ren); g_ren = nullptr;
        SDL_DestroyWindow(g_win);   g_win = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_SetTextureScaleMode(g_tex, SDL_SCALEMODE_NEAREST);

    BuildScanTable();
    memset(g_held, 0, sizeof(g_held));
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(&g_pad_state, 0, sizeof(g_pad_state));

    if (g_config.fullscreen)
        SDL_SetWindowFullscreen(g_win, true);
    return true;
}

void Plat_Shutdown() {
    if (g_stream) { SDL_DestroyAudioStream(g_stream); g_stream = nullptr; }
    if (g_pad)    { SDL_CloseGamepad(g_pad); g_pad = nullptr; }
    if (g_tex)    { SDL_DestroyTexture(g_tex); g_tex = nullptr; }
    if (g_ren)    { SDL_DestroyRenderer(g_ren); g_ren = nullptr; }
    if (g_win)    { SDL_DestroyWindow(g_win); g_win = nullptr; }
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// Events / input
// ---------------------------------------------------------------------------
bool Plat_PumpEvents() {
    memset(g_pressed, 0, sizeof(g_pressed));

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            return false;

        case SDL_EVENT_KEY_DOWN: {
            // Alt+Enter toggles fullscreen; swallow it so the engine never sees it.
            if ((ev.key.mod & SDL_KMOD_ALT) && ev.key.scancode == SDL_SCANCODE_RETURN) {
                g_config.fullscreen = !g_config.fullscreen;
                SDL_SetWindowFullscreen(g_win, g_config.fullscreen);
                break;
            }
            int sc = (int)ev.key.scancode;
            if (sc >= 0 && sc < (int)(sizeof(g_scan_to_key))) {
                u8 k = g_scan_to_key[sc];
                if (k != 0xFF) {
                    if (!ev.key.repeat)
                        g_pressed[k] = true;
                    g_held[k] = true;
                }
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            int sc = (int)ev.key.scancode;
            if (sc >= 0 && sc < (int)(sizeof(g_scan_to_key))) {
                u8 k = g_scan_to_key[sc];
                if (k != 0xFF)
                    g_held[k] = false;
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!g_pad) {
                g_pad = SDL_OpenGamepad(ev.gdevice.which);
                if (!g_pad)
                    fprintf(stderr, "Plat_PumpEvents: SDL_OpenGamepad failed: %s\n",
                            SDL_GetError());
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (g_pad && ev.gdevice.which == SDL_GetGamepadID(g_pad)) {
                SDL_CloseGamepad(g_pad);
                g_pad = nullptr;
            }
            break;
        default:
            break;
        }
    }

    // Snapshot gamepad state once per pump.
    memset(&g_pad_state, 0, sizeof(g_pad_state));
    if (g_pad) {
        g_pad_state.connected = true;
        g_pad_state.button[PB_SOUTH]      = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_SOUTH);
        g_pad_state.button[PB_EAST]       = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_EAST);
        g_pad_state.button[PB_WEST]       = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_WEST);
        g_pad_state.button[PB_NORTH]      = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_NORTH);
        g_pad_state.button[PB_DPAD_UP]    = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        g_pad_state.button[PB_DPAD_DOWN]  = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        g_pad_state.button[PB_DPAD_LEFT]  = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        g_pad_state.button[PB_DPAD_RIGHT] = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        g_pad_state.button[PB_L1]         = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        g_pad_state.button[PB_R1]         = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        g_pad_state.button[PB_START]      = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_START);
        g_pad_state.button[PB_BACK]       = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_BACK);
        g_pad_state.button[PB_LSTICK]     = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
        g_pad_state.button[PB_RSTICK]     = SDL_GetGamepadButton(g_pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        // Analog triggers exposed as digital buttons, PS1 style.
        g_pad_state.button[PB_L2] =
            SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000;
        g_pad_state.button[PB_R2] =
            SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
        g_pad_state.lx = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX);
        g_pad_state.ly = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY);
        g_pad_state.rx = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTX);
        g_pad_state.ry = SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTY);
    }
    return true;
}

bool Plat_KeyHeld(PlatKey k)    { return k < PK_COUNT && g_held[k]; }
bool Plat_KeyPressed(PlatKey k) { return k < PK_COUNT && g_pressed[k]; }
const GamepadState* Plat_Gamepad() { return &g_pad_state; }

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------
void Plat_Present(const u32* pixels, int w, int h) {
    if (!g_ren || !g_tex || !pixels || w <= 0 || h <= 0)
        return;
    if (w > FB_MAX_W) w = FB_MAX_W;
    if (h > FB_MAX_H) h = FB_MAX_H;

    SDL_Rect src = { 0, 0, w, h };
    SDL_UpdateTexture(g_tex, &src, pixels, w * 4);

    int winw = 0, winh = 0;
    SDL_GetWindowSize(g_win, &winw, &winh);
    if (winw <= 0) winw = w;
    if (winh <= 0) winh = h;

    // float math permitted here: presentation-only, never touches game logic.
    SDL_FRect dst;
    switch (g_config.scale_mode) {
    case SCALE_INTEGER: {
        int nx = winw / w, ny = winh / h;
        int n = nx < ny ? nx : ny;
        if (n < 1) n = 1;
        dst.w = (float)(n * w);
        dst.h = (float)(n * h);
        dst.x = (float)((winw - n * w) / 2);
        dst.y = (float)((winh - n * h) / 2);
        break;
    }
    case SCALE_FIT: {
        float sx = (float)winw / (float)w;
        float sy = (float)winh / (float)h;
        float s = sx < sy ? sx : sy;
        dst.w = (float)w * s;
        dst.h = (float)h * s;
        dst.x = ((float)winw - dst.w) * 0.5f;
        dst.y = ((float)winh - dst.h) * 0.5f;
        break;
    }
    case SCALE_STRETCH:
    default:
        dst.x = 0.0f; dst.y = 0.0f;
        dst.w = (float)winw; dst.h = (float)winh;
        break;
    }

    SDL_FRect srcf = { 0.0f, 0.0f, (float)w, (float)h };
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    SDL_RenderTexture(g_ren, g_tex, &srcf, &dst);
    SDL_RenderPresent(g_ren);
}

int Plat_WindowW() {
    int w = 0, h = 0;
    if (g_win) SDL_GetWindowSize(g_win, &w, &h);
    return w;
}

int Plat_WindowH() {
    int w = 0, h = 0;
    if (g_win) SDL_GetWindowSize(g_win, &w, &h);
    return h;
}

void Plat_SetFullscreen(bool fs) {
    g_config.fullscreen = fs;
    if (g_win)
        SDL_SetWindowFullscreen(g_win, fs);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
u64  Plat_TicksNS()        { return SDL_GetTicksNS(); }
void Plat_SleepNS(u64 ns)  { SDL_DelayNS(ns); }

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
static void SDLCALL AudioStreamCb(void* userdata, SDL_AudioStream* stream,
                                  int additional_amount, int total_amount) {
    (void)userdata; (void)total_amount;
    static i16 buf[4096 * 2];              // 4096 stereo frames per chunk
    const int kMaxBytes = 4096 * 4;        // frame = 2ch * 2 bytes
    while (additional_amount > 0) {
        int bytes = additional_amount < kMaxBytes ? additional_amount : kMaxBytes;
        int frames = bytes / 4;
        if (frames <= 0)
            break;                          // sub-frame remainder: drop it
        g_audio_cb(buf, frames, g_audio_user);
        SDL_PutAudioStreamData(stream, buf, frames * 4);
        additional_amount -= frames * 4;
    }
}

bool Plat_StartAudio(int rate, PlatAudioCallback cb, void* user) {
    if (!cb) {
        fprintf(stderr, "Plat_StartAudio: null callback\n");
        return false;
    }
    g_audio_cb = cb;
    g_audio_user = user;

    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = 2;
    spec.freq     = rate;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, AudioStreamCb, nullptr);
    if (!g_stream) {
        fprintf(stderr, "Plat_StartAudio: SDL_OpenAudioDeviceStream failed: %s\n",
                SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------
u8* Plat_ReadFile(const char* path, u32* size) {
    if (size) *size = 0;
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Plat_ReadFile: cannot open '%s'\n", path);
        return nullptr;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Plat_ReadFile: seek failed '%s'\n", path);
        fclose(f);
        return nullptr;
    }
    long len = ftell(f);
    if (len < 0) {
        fprintf(stderr, "Plat_ReadFile: ftell failed '%s'\n", path);
        fclose(f);
        return nullptr;
    }
    fseek(f, 0, SEEK_SET);
    u8* data = (u8*)malloc(len > 0 ? (size_t)len : 1);
    if (!data) {
        fprintf(stderr, "Plat_ReadFile: out of memory (%ld bytes) '%s'\n", len, path);
        fclose(f);
        return nullptr;
    }
    if (len > 0 && fread(data, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "Plat_ReadFile: short read '%s'\n", path);
        free(data);
        fclose(f);
        return nullptr;
    }
    fclose(f);
    if (size) *size = (u32)len;
    return data;
}

bool Plat_WriteFile(const char* path, const void* data, u32 size) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Plat_WriteFile: cannot open '%s'\n", path);
        return false;
    }
    bool ok = (size == 0) || (fwrite(data, 1, size, f) == size);
    if (!ok)
        fprintf(stderr, "Plat_WriteFile: short write '%s'\n", path);
    fclose(f);
    return ok;
}

void Plat_MkDir(const char* path) {
    if (_mkdir(path) != 0 && errno != EEXIST)
        fprintf(stderr, "Plat_MkDir: failed to create '%s'\n", path);
}
