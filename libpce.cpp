#include "corelib.h"
#include "ring.hpp"
#include "Geargrafx/src/geargrafx_core.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#define DBG true

#ifndef DBG
#define DBG false
#endif

#if DBG
#include <stdio.h>
#else
void printf(const char* msg, ...) {}
#endif

#define REQUIRE_CORE(val) if (!has_init_) { printf("no core. skipping %s\n", __func__); return val; }

GeargrafxCore core_;
bool has_init_ = false;
uint32_t megabuffer[OVER_WIDTH * OVER_HEIGHT];  // core needs to dump w/ overscan somewhere
uint32_t fbuffer[VIDEO_WIDTH * VIDEO_HEIGHT];   // non-overscan buffer.
int16_t abuffer[SAMPLE_RATE];  // buffer at most 1sec audio
Ring<int16_t, SAMPLE_RATE> ring_;

void (*corelib_puts)(const char* msg);

EXPOSE
void corelib_set_puts(void(*cb)(const char*)) {
    corelib_puts = cb;
    corelib_puts("corelib_puts initialized");
}
#ifndef ISWASM
#define puts(msg) corelib_puts(msg);
#endif

void log(const char* msg) {
#ifdef DBG
    puts(msg);
#endif
}

EXPOSE
void zero() {
    printf("zeroing.\n");
}

EXPOSE
void set_key(size_t key, char val) {
    GG_Keys gsk;
    switch(key) {
        case BTN_A: gsk = GG_KEY_I; break;
        case BTN_B: gsk = GG_KEY_II; break;
        /* Keys III to VI also exist, but
           don't exist on the main controller
           so are unmapped here.
        */
        case BTN_Up: gsk = GG_KEY_UP; break;
        case BTN_Down: gsk = GG_KEY_DOWN; break;
        case BTN_Left: gsk = GG_KEY_LEFT; break;
        case BTN_Right: gsk = GG_KEY_RIGHT; break;
        default: return;
    }

    constexpr GG_Controllers joypad = GG_CONTROLLER_1;
    if (val) {
        core_.KeyPressed(joypad, gsk);
    } else {
        core_.KeyReleased(joypad, gsk);
    }
}

uint8_t rom_buffer_[10*1024*1024];
// frontend needs a wasm buffer into which it can copy the rom.
EXPOSE
uint8_t *alloc_rom(size_t bytes) {
    if (bytes > sizeof(rom_buffer_)) {
        printf("rom too large, failed to load\n");
         return 0;
    }
    return rom_buffer_;
}

void unused_input_pump() {}

EXPOSE
void init(const uint8_t* data, size_t len) {
    printf("libsms init\n");

    core_.Init(
            unused_input_pump,
            GG_PIXEL_RGBA8888);
    if (!core_.LoadHuCardFromBuffer(data, len, "game.pce")) {
        puts("ROM load failed.");
        return;
    }
    has_init_ = true;

    GG_Runtime_Info runtime_info;
    core_.GetRuntimeInfo(runtime_info);
    printf("screen width: %d\n", runtime_info.screen_width);
    printf("screen height: %d\n", runtime_info.screen_height);
    assert(runtime_info.screen_width == VIDEO_WIDTH);
    assert(runtime_info.screen_height == VIDEO_HEIGHT);
}

EXPOSE
const uint8_t *framebuffer() {
#ifdef ISWASM
    // ensure all pixels have 255 alpha
    for (int i = 0; i < VIDEO_WIDTH * VIDEO_HEIGHT; i++) {
        fbuffer[i] |= 0xff000000;
    }
#endif
    return (const uint8_t*)fbuffer;
}

extern "C" 
__attribute__((visibility("default")))
size_t framebuffer_bytes() {
    return sizeof fbuffer;
}

size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

int16_t last_sample_ = 0;
EXPOSE
long apu_sample_variable(int16_t* output, int32_t samples) {
    REQUIRE_CORE(0);
    size_t read = ring_.pull(output, samples);
    if (read > 0) {
        last_sample_ = output[read - 1];
    }
    // if (read < samples) {
    //     printf("underflow: want=%u, had=%zu\n", samples, read);
    // }
    for (int i = read; read < samples; read++) {
        output[i] = last_sample_;
    }
    return read;
}

EXPOSE
void frame() {
    REQUIRE_CORE();
    int samples = 0;
    // core_.RunToVBlank((uint8_t*)&megabuffer, abuffer, &samples);
    core_.RunToVBlank((uint8_t*)&fbuffer, abuffer, &samples);
    // Core produces stereo, convert to mono
    for (int i = 0; i < samples/2; i++) {
        abuffer[i] = abuffer[2*i];
    }
    int pushed = ring_.push(abuffer, samples/2);
    if (pushed != samples) {
        printf("ring overflow: %d / %d pushed\n", pushed, samples);
    }
    // auto *video = core_.GetVideo();
    // video->Render32bit(video->GetFrameBuffer(), (uint8_t*)fbuffer, GS_PIXEL_RGBA8888, VIDEO_WIDTH*VIDEO_HEIGHT, /*overscan*/false);
}

#ifndef __wasm32__
// save&load unsupported for wasm

constexpr size_t MAX_STATE_SIZE = 100'000'000; // 100M
EXPOSE
void save(int fd) {
    REQUIRE_CORE();
    // Total size is unknown, make a conservative allocation.
    uint8_t *buffer = (uint8_t*)malloc(MAX_STATE_SIZE);
    uint8_t* const orig_buffer = buffer;
    size_t bytes = 0;
    core_.SaveState(buffer, bytes);
    printf("Wrote %zu bytes\n", bytes);
    while (bytes > 0) {
        ssize_t written = write(fd, buffer, bytes);
        if (written <= 0) {
            perror("Save failed: ");
            return;
        }
        bytes -= written;
        buffer += written;
    }
    free(orig_buffer);
}


EXPOSE
void dump_state(const char* filename) {
    REQUIRE_CORE();
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY , 0700);
    if (fd == -1) {
        perror("failed to open:");
        return;
    }
    printf("saving to %s\n", filename);
    save(fd);
}


EXPOSE
void load(int fd) {
    REQUIRE_CORE();
    ssize_t bytes = lseek(fd, 0, SEEK_END);
    if (bytes <= 0) {
        perror("Failed to seek while loading: ");
        return;
    }
    const size_t state_size = bytes;
    printf("Loading %zu bytes\n", bytes);
    lseek(fd, 0, SEEK_SET);
    uint8_t *buffer = (uint8_t*)malloc(bytes);
    uint8_t *write = buffer;
    while (bytes > 0) {
        ssize_t read_bytes = read(fd, write, bytes);
        if (read_bytes <= 0) {
            perror("Read failure during load: ");
            return;
        }
        printf("read returned %zu bytes\n", read_bytes);
        write += read_bytes;
        bytes -= read_bytes;
    }
    core_.LoadState(buffer, state_size);
    free(buffer);
}

EXPOSE
void load_state(const char* filename) {
    REQUIRE_CORE();
    int fd = open(filename,  O_RDONLY , 0700);
    if (fd == -1) {
        perror("Failed to open: ");
        return;
    }
    load(fd);
}

#endif // ifndef __wasm32__
