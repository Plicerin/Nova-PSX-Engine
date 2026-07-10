// Virtual PS1 pad: merges keyboard (primary + secondary key) and gamepad.
#include "engine/input/input.h"

#include <cstdio>
#include <cstring>
#include <cctype>

static const u8 NO_BIND = 0xFF;

struct Binding {
    u8 key_primary;   // PlatKey or NO_BIND
    u8 key_secondary; // PlatKey or NO_BIND
    u8 hw;            // PadHwButton or NO_BIND
};

static Binding s_bind[PAD_COUNT];
static bool    s_held[PAD_COUNT];
static bool    s_prev[PAD_COUNT];

static const i16 STICK_DPAD_THRESHOLD = 16384; // half deflection counts as dpad
static const i16 STICK_DEADZONE       = 8000;

// ---------------------------------------------------------------- defaults

static void SetBind(PadButton b, u8 kp, u8 ks, u8 hw)
{
    s_bind[b].key_primary   = kp;
    s_bind[b].key_secondary = ks;
    s_bind[b].hw            = hw;
}

void Input_Init()
{
    memset(s_held, 0, sizeof(s_held));
    memset(s_prev, 0, sizeof(s_prev));

    SetBind(PAD_UP,       PK_UP,        PK_W,    PB_DPAD_UP);
    SetBind(PAD_DOWN,     PK_DOWN,      PK_S,    PB_DPAD_DOWN);
    SetBind(PAD_LEFT,     PK_LEFT,      PK_A,    PB_DPAD_LEFT);
    SetBind(PAD_RIGHT,    PK_RIGHT,     PK_D,    PB_DPAD_RIGHT);
    SetBind(PAD_CROSS,    PK_SPACE,     NO_BIND, PB_SOUTH);
    SetBind(PAD_CIRCLE,   PK_BACKSPACE, NO_BIND, PB_EAST);
    SetBind(PAD_SQUARE,   PK_Q,         NO_BIND, PB_WEST);
    SetBind(PAD_TRIANGLE, PK_E,         NO_BIND, PB_NORTH);
    SetBind(PAD_L1,       PK_COMMA,     NO_BIND, PB_L1);
    SetBind(PAD_R1,       PK_PERIOD,    NO_BIND, PB_R1);
    SetBind(PAD_L2,       PK_MINUS,     NO_BIND, PB_L2);
    SetBind(PAD_R2,       PK_EQUALS,    NO_BIND, PB_R2);
    SetBind(PAD_START,    PK_RETURN,    NO_BIND, PB_START);
    // Tab is the debug free-camera toggle, so Select uses LShift instead.
    SetBind(PAD_SELECT,   PK_LSHIFT,    NO_BIND, PB_BACK);
}

// ---------------------------------------------------------------- update

static bool KeyHeldFor(PadButton b)
{
    const Binding& bd = s_bind[b];
    if (bd.key_primary   != NO_BIND && Plat_KeyHeld((PlatKey)bd.key_primary))   return true;
    if (bd.key_secondary != NO_BIND && Plat_KeyHeld((PlatKey)bd.key_secondary)) return true;
    return false;
}

void Input_Update()
{
    memcpy(s_prev, s_held, sizeof(s_prev));

    const GamepadState* gp = Plat_Gamepad();
    const bool pad_ok = gp && gp->connected;

    for (int i = 0; i < PAD_COUNT; ++i) {
        PadButton b = (PadButton)i;
        bool held = KeyHeldFor(b);
        if (!held && pad_ok && s_bind[i].hw != NO_BIND)
            held = gp->button[s_bind[i].hw];
        if (!held && pad_ok) {
            // Left stick doubles as dpad past half deflection (y-down like SDL).
            switch (b) {
            case PAD_UP:    held = gp->ly < -STICK_DPAD_THRESHOLD; break;
            case PAD_DOWN:  held = gp->ly >  STICK_DPAD_THRESHOLD; break;
            case PAD_LEFT:  held = gp->lx < -STICK_DPAD_THRESHOLD; break;
            case PAD_RIGHT: held = gp->lx >  STICK_DPAD_THRESHOLD; break;
            default: break;
            }
        }
        s_held[i] = held;
    }
}

void Input_ConsumeEdges()
{
    // The main loop may run several fixed sim ticks per rendered frame; only
    // the first tick may see press/release edges, or one keypress toggles a
    // menu two or three times. The loop calls this after every sim tick.
    memcpy(s_prev, s_held, sizeof(s_prev));
    Plat_ClearPressed();
}

bool Pad_Held(PadButton b)     { return b < PAD_COUNT && s_held[b]; }
bool Pad_Pressed(PadButton b)  { return b < PAD_COUNT && s_held[b] && !s_prev[b]; }
bool Pad_Released(PadButton b) { return b < PAD_COUNT && !s_held[b] && s_prev[b]; }

static int StickAxis(i16 raw)
{
    if (raw > -STICK_DEADZONE && raw < STICK_DEADZONE) return 0;
    return raw / 256; // -128..127
}

int Pad_StickX()
{
    int v = 0;
    const GamepadState* gp = Plat_Gamepad();
    if (gp && gp->connected) v = StickAxis(gp->lx);
    const bool l = KeyHeldFor(PAD_LEFT);
    const bool r = KeyHeldFor(PAD_RIGHT);
    if (l || r) v = (r ? 127 : 0) - (l ? 127 : 0);
    return v;
}

int Pad_StickY()
{
    // Engine y grows down; SDL LEFTY is already positive-down, so no flip.
    int v = 0;
    const GamepadState* gp = Plat_Gamepad();
    if (gp && gp->connected) v = StickAxis(gp->ly);
    const bool u = KeyHeldFor(PAD_UP);
    const bool d = KeyHeldFor(PAD_DOWN);
    if (u || d) v = (d ? 127 : 0) - (u ? 127 : 0);
    return v;
}

// ---------------------------------------------------------------- binding

void Input_BindKey(PadButton b, PlatKey key)
{
    if (b >= PAD_COUNT || key >= PK_COUNT) return;
    s_bind[b].key_primary = (u8)key;
}

void Input_BindPad(PadButton b, PadHwButton hw)
{
    if (b >= PAD_COUNT || hw >= PB_HW_COUNT) return;
    s_bind[b].hw = (u8)hw;
}

// ---------------------------------------------------------------- config file

static const char* const kButtonNames[PAD_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE",
    "L1", "R1", "L2", "R2",
    "START", "SELECT",
};

static const char* const kKeyNames[PK_COUNT] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "UP", "DOWN", "LEFT", "RIGHT",
    "ESCAPE", "RETURN", "SPACE", "TAB", "LSHIFT", "LCTRL", "BACKSPACE",
    "COMMA", "PERIOD", "MINUS", "EQUALS",
};

static const char* const kHwNames[PB_HW_COUNT] = {
    "SOUTH", "EAST", "WEST", "NORTH",
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT",
    "L1", "R1", "L2", "R2",
    "START", "BACK", "LSTICK", "RSTICK",
};

static bool StrIEq(const char* a, const char* b)
{
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

static int FindName(const char* const* table, int count, const char* name)
{
    for (int i = 0; i < count; ++i)
        if (StrIEq(table[i], name)) return i;
    return -1;
}

static char* Trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

void Input_LoadBindings(const char* path)
{
    if (!path) return;
    FILE* f = fopen(path, "r");
    if (!f) return; // missing file: keep defaults, no complaint

    char line[128];
    while (fgets(line, (int)sizeof(line), f)) {
        char* s = Trim(line);
        if (!*s || *s == '#' || *s == ';') continue;

        char* eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "input: bad line in %s: '%s'\n", path, s);
            continue;
        }
        *eq = '\0';
        char* lhs = Trim(s);
        char* rhs = Trim(eq + 1);

        char* dot = strchr(lhs, '.');
        if (!dot) {
            fprintf(stderr, "input: bad line in %s: '%s'\n", path, lhs);
            continue;
        }
        *dot = '\0';
        const char* kind = Trim(lhs);
        const char* bname = Trim(dot + 1);

        int b = FindName(kButtonNames, PAD_COUNT, bname);
        if (b < 0) {
            fprintf(stderr, "input: unknown pad button '%s' in %s\n", bname, path);
            continue;
        }

        if (StrIEq(kind, "KEY")) {
            int k = FindName(kKeyNames, PK_COUNT, rhs);
            if (k < 0) fprintf(stderr, "input: unknown key '%s' in %s\n", rhs, path);
            else       Input_BindKey((PadButton)b, (PlatKey)k);
        } else if (StrIEq(kind, "PAD")) {
            int h = FindName(kHwNames, PB_HW_COUNT, rhs);
            if (h < 0) fprintf(stderr, "input: unknown pad hw button '%s' in %s\n", rhs, path);
            else       Input_BindPad((PadButton)b, (PadHwButton)h);
        } else {
            fprintf(stderr, "input: unknown binding kind '%s' in %s\n", kind, path);
        }
    }
    fclose(f);
}
