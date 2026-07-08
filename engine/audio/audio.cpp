// PS1-flavored voice mixer (spec 9). Zero-order-hold resampling on purpose:
// the aliasing crunch is the authentic constraint, do not "fix" it.
#include "engine/audio/audio.h"
#include "engine/platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

constexpr int MAX_SAMPLES = 128;

struct Voice {
    const Sample* s;
    u32  pos_fx;     // 20.12 frame position
    u32  step_fx;    // 20.12 source frames per output frame
    i32  volL, volR;
    bool active;
};

Sample g_samples[MAX_SAMPLES];
int    g_sample_count = 0;
u32    g_used_bytes   = 0;   // non-music only

Voice      g_voices[AUDIO_MAX_VOICES];
std::mutex g_lock;           // Play/Stop/etc (game thread) vs Audio_Mix (audio thread)

u16 RdU16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
u32 RdU32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void PanVolumes(int vol, int pan, i32* volL, i32* volR) {
    if (vol < 0) vol = 0;
    if (vol > 128) vol = 128;
    if (pan < -128) pan = -128;
    if (pan > 127) pan = 127;
    int l = 128 - pan; if (l > 128) l = 128;
    int r = 128 + pan; if (r > 128) r = 128;
    *volL = (vol * l) >> 7;
    *volR = (vol * r) >> 7;
}

} // namespace

bool Audio_Init() {
    std::lock_guard<std::mutex> lock(g_lock);
    memset(g_voices, 0, sizeof(g_voices));
    return true;
}

Sample* Audio_LoadWav(const char* path, const char* name, bool is_music, bool loop_whole) {
    u32 size = 0;
    u8* buf = Plat_ReadFile(path, &size);
    if (!buf) {
        fprintf(stderr, "audio: cannot read '%s'\n", path);
        return nullptr;
    }
    if (size < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "audio: '%s' is not a RIFF/WAVE file\n", path);
        free(buf);
        return nullptr;
    }

    u16 fmt_tag = 0, channels = 0, bits = 0;
    u32 rate = 0;
    bool have_fmt = false;
    const u8* data_ptr = nullptr;
    u32 data_size = 0;

    u32 off = 12;
    while (off + 8 <= size) {
        const u8* id = buf + off;
        u32 csz = RdU32(buf + off + 4);
        u32 body = off + 8;
        if (csz > size - body) {
            fprintf(stderr, "audio: '%s' has a truncated chunk\n", path);
            break;
        }
        if (memcmp(id, "fmt ", 4) == 0 && csz >= 16) {
            fmt_tag  = RdU16(buf + body);
            channels = RdU16(buf + body + 2);
            rate     = RdU32(buf + body + 4);
            bits     = RdU16(buf + body + 14);
            have_fmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            data_ptr  = buf + body;
            data_size = csz;
        }
        off = body + csz + (csz & 1);   // RIFF chunks are word-aligned
    }

    if (!have_fmt || !data_ptr) {
        fprintf(stderr, "audio: '%s' missing fmt/data chunk\n", path);
        free(buf);
        return nullptr;
    }
    if (fmt_tag != 1) {
        fprintf(stderr, "audio: '%s' format %u unsupported (PCM only)\n", path, fmt_tag);
        free(buf);
        return nullptr;
    }
    if (bits != 8 && bits != 16) {
        fprintf(stderr, "audio: '%s' has %u-bit samples (need 8 or 16)\n", path, bits);
        free(buf);
        return nullptr;
    }
    if (channels < 1 || channels > 2) {
        fprintf(stderr, "audio: '%s' has %u channels (need 1 or 2)\n", path, channels);
        free(buf);
        return nullptr;
    }
    if (rate != 11025 && rate != 22050 && rate != 44100) {
        fprintf(stderr, "audio: '%s' rate %u not in {11025,22050,44100}\n", path, rate);
        free(buf);
        return nullptr;
    }

    u32 frame_bytes = (u32)channels * (bits / 8u);
    u32 frames = data_size / frame_bytes;
    if (frames == 0) {
        fprintf(stderr, "audio: '%s' has no sample data\n", path);
        free(buf);
        return nullptr;
    }
    if (frames >= (1u << 20)) {   // 20.12 voice position can't address more
        fprintf(stderr, "audio: '%s' too long for 20.12 playback, truncating\n", path);
        frames = (1u << 20) - 1;
    }

    u32 bytes = frames * 2;
    if (!is_music && g_used_bytes + bytes > AUDIO_BUDGET_BYTES) {
        fprintf(stderr, "audio: '%s' blows SFX budget (%u + %u > %u)\n",
                path, g_used_bytes, bytes, AUDIO_BUDGET_BYTES);
        free(buf);
        return nullptr;
    }
    if (g_sample_count >= MAX_SAMPLES) {
        fprintf(stderr, "audio: sample table full (%d) loading '%s'\n", MAX_SAMPLES, path);
        free(buf);
        return nullptr;
    }

    i16* out = (i16*)malloc(bytes);
    if (!out) {
        fprintf(stderr, "audio: out of memory for '%s' (%u bytes)\n", path, bytes);
        free(buf);
        return nullptr;
    }
    for (u32 f = 0; f < frames; ++f) {
        const u8* src = data_ptr + (size_t)f * frame_bytes;
        i32 sum = 0;
        for (u32 c = 0; c < channels; ++c) {
            if (bits == 8) sum += ((i32)src[c] - 128) << 8;
            else           sum += (i16)RdU16(src + c * 2);
        }
        out[f] = (i16)(sum / (i32)channels);
    }
    free(buf);

    Sample* smp = &g_samples[g_sample_count++];
    memset(smp, 0, sizeof(*smp));
    snprintf(smp->name, sizeof(smp->name), "%s", name ? name : "");
    smp->rate       = (i32)rate;
    smp->frames     = frames;
    smp->loop_start = loop_whole ? 0 : -1;
    smp->data       = out;
    smp->bytes      = bytes;
    smp->is_music   = is_music ? 1 : 0;
    if (!is_music) g_used_bytes += bytes;
    return smp;
}

u32 Audio_UsedBytes() {
    return g_used_bytes;
}

int Audio_Play(const Sample* s, int vol, int pan, int pitch) {
    if (!s || !s->data || s->frames == 0) return -1;
    if (pitch < 1) pitch = 1;
    u32 step = (u32)(((u64)s->rate << 12) * (u64)pitch / 4096 / AUDIO_RATE);
    if (step == 0) step = 1;

    std::lock_guard<std::mutex> lock(g_lock);
    for (int i = 0; i < AUDIO_MAX_VOICES; ++i) {
        Voice& v = g_voices[i];
        if (v.active) continue;
        v.s       = s;
        v.pos_fx  = 0;
        v.step_fx = step;
        PanVolumes(vol, pan, &v.volL, &v.volR);
        v.active  = true;
        return i;
    }
    return -1;   // no stealing: caller manages voices, PS1-style
}

void Audio_Stop(int voice) {
    if (voice < 0 || voice >= AUDIO_MAX_VOICES) return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_voices[voice].active = false;
}

void Audio_StopAll() {
    std::lock_guard<std::mutex> lock(g_lock);
    for (int i = 0; i < AUDIO_MAX_VOICES; ++i) g_voices[i].active = false;
}

void Audio_SetVolume(int voice, int vol, int pan) {
    if (voice < 0 || voice >= AUDIO_MAX_VOICES) return;
    std::lock_guard<std::mutex> lock(g_lock);
    PanVolumes(vol, pan, &g_voices[voice].volL, &g_voices[voice].volR);
}

int Audio_ActiveVoices() {
    std::lock_guard<std::mutex> lock(g_lock);
    int n = 0;
    for (int i = 0; i < AUDIO_MAX_VOICES; ++i)
        if (g_voices[i].active) ++n;
    return n;
}

void Audio_Mix(i16* out_stereo, int frames) {
    if (!out_stereo || frames <= 0) return;
    memset(out_stereo, 0, (size_t)frames * 2 * sizeof(i16));

    std::lock_guard<std::mutex> lock(g_lock);
    for (int f = 0; f < frames; ++f) {
        i32 accL = 0, accR = 0;
        for (int vi = 0; vi < AUDIO_MAX_VOICES; ++vi) {
            Voice& v = g_voices[vi];
            if (!v.active) continue;
            const Sample* s = v.s;
            u32 idx = v.pos_fx >> 12;
            if (idx >= s->frames) { v.active = false; continue; }

            i32 smp = s->data[idx];   // zero-order hold, no interpolation
            accL += (smp * v.volL) >> 7;
            accR += (smp * v.volR) >> 7;

            u64 np  = (u64)v.pos_fx + v.step_fx;
            u64 end = (u64)s->frames << 12;
            if (np < end) {
                v.pos_fx = (u32)np;
            } else if (s->loop_start >= 0 && ((u64)s->loop_start << 12) < end) {
                do { np = ((u64)s->loop_start << 12) + (np - end); } while (np >= end);
                v.pos_fx = (u32)np;   // overshoot preserved across the wrap
            } else {
                v.active = false;
            }
        }
        if (accL > 32767) accL = 32767; else if (accL < -32768) accL = -32768;
        if (accR > 32767) accR = 32767; else if (accR < -32768) accR = -32768;
        out_stereo[f * 2 + 0] = (i16)accL;
        out_stereo[f * 2 + 1] = (i16)accR;
    }
}
