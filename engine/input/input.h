// Virtual PS1-style pad on top of keyboard + gamepad (spec 10).
// Default mapping:
//   D-pad/left stick  <- arrows/WASD, gamepad dpad + left stick
//   Cross/A confirm   <- Space / Enter,          gamepad South
//   Circle/B cancel   <- Backspace / Escape(nav),gamepad East
//   Square/X secondary<- Q,                      gamepad West
//   Triangle/Y special<- E,                      gamepad North
//   L1/R1             <- , / . (camera),         gamepad shoulders
//   L2/R2             <- - / =,                  gamepad triggers
//   Start             <- Return, gamepad Start;  Select <- Tab, gamepad Back
// Remappable via Input_Bind (and bindings file if present).
#pragma once
#include "engine/core/types.h"
#include "engine/platform/platform.h"

enum PadButton : u8 {
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
    PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE,
    PAD_L1, PAD_R1, PAD_L2, PAD_R2,
    PAD_START, PAD_SELECT,
    PAD_COUNT
};

void Input_Init();                       // installs default bindings
void Input_Update();                     // call once per frame after Plat_PumpEvents
bool Pad_Held(PadButton b);
bool Pad_Pressed(PadButton b);           // edge since last frame
bool Pad_Released(PadButton b);
// Left stick with keyboard fallback (digital -> full deflection), -128..127.
int  Pad_StickX();
int  Pad_StickY();

// Remapping: bind a keyboard key and/or hardware pad button to a pad button.
void Input_BindKey(PadButton b, PlatKey key);
void Input_BindPad(PadButton b, PadHwButton hw);
// Load "key.CROSS=SPACE"-style overrides; missing file is fine (defaults stay).
void Input_LoadBindings(const char* path);
