// Fixed-point math implementation. Sine table is built once with float math
// at init time only; all runtime queries are pure integer (determinism-safe).
#include "engine/math/fixed.h"
#include <cmath>

static i16 sin_table[4096];

void Fixed_Init() {
    for (int i = 0; i < 4096; ++i) {
        float f = sinf((float)i * (2.0f * 3.14159265358979323846f) / 4096.0f);
        long s = lround(f * 4096.0f);
        if (s < -4096) s = -4096;
        if (s > 4096)  s = 4096;
        sin_table[i] = (i16)s;
    }
}

fx12 Csin(i32 angle) { return sin_table[angle & 4095]; }
fx12 Ccos(i32 angle) { return sin_table[(angle + 1024) & 4095]; }

// Bit-by-bit method: exact floor(sqrt(v)) with no float involvement.
u32 IsqrtU32(u32 v) {
    u32 res = 0;
    u32 bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}
