// Platform layer interface (SDL3 implementation in platform_sdl3.cpp).
// The engine never includes SDL headers outside that one file.
#pragma once
#include "engine/core/types.h"

// Keys the engine cares about (mapped to SDL scancodes internally).
enum PlatKey : u8 {
    PK_A, PK_B, PK_C, PK_D, PK_E, PK_F, PK_G, PK_H, PK_I, PK_J, PK_K, PK_L, PK_M,
    PK_N, PK_O, PK_P, PK_Q, PK_R, PK_S, PK_T, PK_U, PK_V, PK_W, PK_X, PK_Y, PK_Z,
    PK_0, PK_1, PK_2, PK_3, PK_4, PK_5, PK_6, PK_7, PK_8, PK_9,
    PK_F1, PK_F2, PK_F3, PK_F4, PK_F5, PK_F6, PK_F7, PK_F8, PK_F9, PK_F10, PK_F11, PK_F12,
    PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT,
    PK_ESCAPE, PK_RETURN, PK_SPACE, PK_TAB, PK_LSHIFT, PK_LCTRL, PK_BACKSPACE,
    PK_COMMA, PK_PERIOD, PK_MINUS, PK_EQUALS,
    PK_COUNT
};

// Gamepad snapshot (SDL gamepad = XInput-style layout).
enum PadHwButton : u8 {
    PB_SOUTH, PB_EAST, PB_WEST, PB_NORTH,          // A B X Y
    PB_DPAD_UP, PB_DPAD_DOWN, PB_DPAD_LEFT, PB_DPAD_RIGHT,
    PB_L1, PB_R1, PB_L2, PB_R2,                    // shoulders; triggers as buttons
    PB_START, PB_BACK, PB_LSTICK, PB_RSTICK,
    PB_HW_COUNT
};

struct GamepadState {
    bool connected;
    bool button[PB_HW_COUNT];
    i16  lx, ly, rx, ry;      // -32768..32767
};

bool Plat_Init(const char* title, int win_w, int win_h);
void Plat_Shutdown();

// Pump events; updates key/pad state and pressed edges. Returns false on quit.
bool Plat_PumpEvents();
bool Plat_KeyHeld(PlatKey k);
bool Plat_KeyPressed(PlatKey k);      // went down since last pump
void Plat_ClearPressed();             // consume key press edges (sim tick ran)
const GamepadState* Plat_Gamepad();

// Upload the internal framebuffer and present with nearest-neighbor scaling
// per g_config.scale_mode (integer/fit/stretch + black bars). VSync on.
void Plat_Present(const u32* pixels, int w, int h);
int  Plat_WindowW();
int  Plat_WindowH();
void Plat_SetFullscreen(bool fs);

u64  Plat_TicksNS();
void Plat_SleepNS(u64 ns);

typedef void (*PlatAudioCallback)(i16* stereo_out, int frames, void* user);
bool Plat_StartAudio(int rate, PlatAudioCallback cb, void* user);

// Read entire file, malloc'd; caller frees. Null + size 0 on failure.
u8*  Plat_ReadFile(const char* path, u32* size);
bool Plat_WriteFile(const char* path, const void* data, u32 size);
void Plat_MkDir(const char* path);
