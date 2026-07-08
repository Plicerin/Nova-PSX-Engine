// PS1-inspired audio (spec 9): sample playback mixer, not an SPU emulator.
// All samples are mono 16-bit PCM at 11025/22050/44100 Hz, mixed to stereo
// 44100. Nearest-sample (zero-order-hold) resampling on purpose — that crunch
// is the authentic constraint. 24 voices, 512 KB simulated sound RAM for SFX;
// music samples are budget-exempt (they stand in for CD streaming).
#pragma once
#include "engine/core/types.h"

constexpr int AUDIO_RATE      = 44100;
constexpr int AUDIO_MAX_VOICES = 24;
constexpr u32 AUDIO_BUDGET_BYTES = 512 * 1024;

struct Sample {
    char name[32];
    i32  rate;           // source rate (11025/22050/44100)
    u32  frames;         // sample count (mono)
    i32  loop_start;     // frame index, -1 = no loop
    i16* data;
    u32  bytes;          // frames*2, counted against budget unless is_music
    u8   is_music;
};

bool    Audio_Init();                       // call after Plat_StartAudio hookup
// Load a WAV (PCM 8/16-bit, mono/stereo -> mono, any of the 3 rates).
// loop_whole: loop from frame 0 (used for music/ambience).
Sample* Audio_LoadWav(const char* path, const char* name, bool is_music, bool loop_whole);
u32     Audio_UsedBytes();                  // SFX budget usage

// vol 0..128, pan -128..+127 (0 center), pitch 4096 = original rate.
// Returns voice index or -1 if none free (steals nothing — PS1 games managed
// voices manually; keep it dumb and visible).
int  Audio_Play(const Sample* s, int vol, int pan, int pitch);
void Audio_Stop(int voice);
void Audio_StopAll();
void Audio_SetVolume(int voice, int vol, int pan);
int  Audio_ActiveVoices();

// Platform audio callback target: mix `frames` stereo frames into out.
// Called from the audio thread; internally locked against Play/Stop.
void Audio_Mix(i16* out_stereo, int frames);
