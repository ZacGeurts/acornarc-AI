#include "libretro.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ctime>
#include <stdarg.h>
#include <zlib.h>
#include "cpu.h"    
#include "memory.h" 
#include "io.h"

uint8_t* floppy_data = nullptr; 
size_t floppy_size = 0;         

static arm3_cpu_t* cpu = nullptr;
static memory_t* memory = nullptr;
static io_t* io = nullptr;
static bool running = false;
static uint16_t* frame_buffer = nullptr;
static const unsigned WIDTH = 320;
static const unsigned HEIGHT = 256;

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t env_cb;
static retro_log_printf_t log_cb = nullptr;
static bool pixel_format_set = false;

static void handle_input(void);
static void render_frame(void);

static void fallback_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void send_message(const char* msg) {
    if (env_cb) {
        struct retro_message retro_msg = {msg, 240};
        env_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &retro_msg);
    } else {
        fallback_log("Message: %s\n", msg);
    }
}

static void init_logging() {
    if (!env_cb || log_cb) return;
    struct retro_log_callback logging;
    if (env_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
        log_cb = logging.log;
    } else {
        fallback_log("Failed to get log interface\n");
    }
}

static void log_message(enum retro_log_level level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (log_cb) {
        log_cb(level, fmt, args);
    } else {
        vfprintf(stderr, fmt, args);
    }
    va_end(args);
}

// Use extern "C" to prevent name mangling for libretro API functions
extern "C" {

void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;
    init_logging();
    log_message(RETRO_LOG_INFO, "retro_set_environment: Callback set\n");

    if (!pixel_format_set) {
        enum retro_pixel_format pf = RETRO_PIXEL_FORMAT_RGB565;
        if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pf)) {
            log_message(RETRO_LOG_ERROR, "Failed to set pixel format to RGB565\n");
            send_message("Core failed to set RGB565 pixel format");
        } else {
            log_message(RETRO_LOG_INFO, "Pixel format set to RGB565\n");
            pixel_format_set = true;
        }
    }

    // Tell RetroArch this core can run without content
    bool no_content = true;
    env_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);
}

void retro_init(void) {
    log_message(RETRO_LOG_INFO, "retro_init called\n");

    io = io_create();
    if (!io) {
        log_message(RETRO_LOG_ERROR, "Failed to initialize I/O module\n");
        send_message("Core failed to initialize I/O module");
        running = false;
        return;
    }

    frame_buffer = (uint16_t*)malloc(WIDTH * HEIGHT * sizeof(uint16_t));
    if (!frame_buffer) {
        log_message(RETRO_LOG_ERROR, "Failed to allocate frame buffer\n");
        send_message("Core failed to allocate frame buffer");
        io_destroy(io);
        io = nullptr;
        running = false;
        return;
    }

    memset(frame_buffer, 0, WIDTH * HEIGHT * sizeof(uint16_t));
    for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
        frame_buffer[i] = 0xF800; // Red screen for now
    }

    running = true;
    log_message(RETRO_LOG_INFO, "retro_init completed successfully\n");
    send_message("Acorn Archimedes Emulator initialized");
}

bool retro_load_game(const struct retro_game_info* game) {
    log_message(RETRO_LOG_INFO, "retro_load_game called\n");

    const char* rom_path = "riscos.rom"; 
    uint32_t rom_base = 0x03800000; // Updated to match new ROM_DEFAULT_BASE
    memory = memory_create(rom_path, rom_base, io);
    if (!memory) {
        log_message(RETRO_LOG_ERROR, "Failed to create memory system with ROM: %s at 0x%08X\n", rom_path, rom_base);
        send_message("Failed to create memory system");
        return false;
    }

    cpu = cpu_create(memory);
    if (!cpu) {
        log_message(RETRO_LOG_ERROR, "Failed to create CPU\n");
        send_message("Failed to create CPU");
        memory_destroy(memory);
        memory = nullptr;
        return false;
    }

    log_message(RETRO_LOG_INFO, "Successfully loaded ROM: %s at 0x%08X\n", rom_path, memory->rom_base);
    send_message("ROM loaded successfully");
    return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info) {
    log_message(RETRO_LOG_WARN, "retro_load_game_special not implemented\n");
    send_message("Special game loading not supported");
    return false;
}

void retro_deinit(void) {
    log_message(RETRO_LOG_INFO, "retro_deinit called\n");
    running = false;
    if (cpu) { cpu_destroy(cpu); cpu = nullptr; }
    if (memory) { memory_destroy(memory); memory = nullptr; }
    if (io) { io_destroy(io); io = nullptr; }
    if (frame_buffer) { free(frame_buffer); frame_buffer = nullptr; }
    if (floppy_data) { free(floppy_data); floppy_data = nullptr; }
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_controller_port_device(unsigned port, unsigned device) {
    log_message(RETRO_LOG_INFO, "Controller port %u set to device %u\n", port, device);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { /* No-op */ }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { /* No-op */ }

void retro_get_system_info(struct retro_system_info* info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Acorn Archimedes Emulator (ARM3)";
    info->library_version = "1.0";
    info->valid_extensions = ""; // No extensions needed
    info->need_fullpath = false; // No content file required
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
    info->geometry.base_width = WIDTH;
    info->geometry.base_height = HEIGHT;
    info->geometry.max_width = WIDTH;
    info->geometry.max_height = HEIGHT;
    info->geometry.aspect_ratio = (float)WIDTH / HEIGHT;
    info->timing.fps = 50.0;
    info->timing.sample_rate = 44100.0;
}

unsigned retro_get_region(void) { return RETRO_REGION_PAL; }
void* retro_get_memory_data(unsigned id) { return nullptr; }
size_t retro_get_memory_size(unsigned id) { return 0; }

void retro_run(void) {
    if (!running || !cpu || !memory) return;

    input_poll_cb();
    handle_input();

    for (unsigned i = 0; i < 160000; i++) {
        uint32_t pc = cpu->registers[15] & ADDR_MASK;
        if (pc > ADDR_MASK) {
            log_message(RETRO_LOG_ERROR, "PC out of bounds: %08x at step %u\n", cpu->registers[15], i);
            running = false;
            send_message("Emulation stopped: PC out of bounds");
            break;
        }
        cpu_step(cpu);
    }

    render_frame();
    if (video_cb && frame_buffer) {
        video_cb(frame_buffer, WIDTH, HEIGHT, WIDTH * sizeof(uint16_t));
    }
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void* data, size_t size) { return false; }
bool retro_unserialize(const void* data, size_t size) { return false; }

void retro_reset(void) {
    log_message(RETRO_LOG_INFO, "retro_reset called\n");
    if (cpu) cpu_reset(cpu);
}

void retro_cheat_reset(void) { /* No-op */ }
void retro_cheat_set(unsigned index, bool enabled, const char* code) {
    log_message(RETRO_LOG_INFO, "Cheat set: index=%u, enabled=%d, code=%s\n", 
                index, enabled, code ? code : "null");
}

void retro_unload_game(void) { /* No-op */ }

} // End of extern "C"

static void handle_input(void) {
    if (input_state_cb && input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_ESCAPE)) {
        log_message(RETRO_LOG_INFO, "Escape key pressed, stopping emulation\n");
        running = false;
    }
}

static void render_frame(void) {
    if (!frame_buffer) return;
    for (unsigned y = 0; y < HEIGHT; y++) {
        for (unsigned x = 0; x < WIDTH; x++) {
            frame_buffer[y * WIDTH + x] = (uint16_t)((x + y + (clock() / 1000)) & 0xFFFF); // Test pattern
        }
    }
}