#pragma once

#include <stdint.h>
#include <stddef.h>

#define VIDEO_WIDTH 240
#define VIDEO_HEIGHT 160
#define OVER_WIDTH VIDEO_WIDTH
#define OVER_HEIGHT VIDEO_HEIGHT
// Used by core to log to ui. Frontends are expected to define this.
void corelib_set_puts(void(*cb)(const char*));

enum Keys {
    BTN_A = 0,
    BTN_B,
    BTN_Sel,
    BTN_Start,
    BTN_Up,
    BTN_Down,
    BTN_Left,
    BTN_Right,
    BTN_L,  // Shoulder buttons
    BTN_R,
    NUM_KEYS
};


#undef EMU_USES_CPP

#ifdef EMU_USES_CPP
#define UNMANGLE extern "C"
#else
#define UNMANGLE
#endif

#define EXPOSE __attribute__((visibility("default")))

UNMANGLE void set_key(size_t key, char val);
UNMANGLE void init(const uint8_t* data, size_t len);
UNMANGLE const uint8_t *framebuffer();
UNMANGLE void frame();
UNMANGLE void dump_state(const char* save_path);
UNMANGLE void load_state(const char* save_path);
// Interface used by app. App closes fd.
UNMANGLE void save(int fd);
UNMANGLE void load(int fd);

// APU
const int SAMPLE_RATE = 44100;
const int SAMPLES_PER_FRAME = SAMPLE_RATE / 60;
UNMANGLE void apu_tick_60hz();
UNMANGLE void apu_sample_60hz(int16_t *output);
UNMANGLE long apu_sample_variable(int16_t *output, int32_t frames);
