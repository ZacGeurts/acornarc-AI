// core.cpp
#include "libretro.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ctime>    // For clock()
#include <stdarg.h> // For va_start, va_end
#include <zlib.h>   // For gzip decompression
#include "cpu.h"    // ARM2 CPU
#include "memory.h" // Memory system

static arm2_cpu_t* cpu = nullptr;
static memory_t* memory = nullptr;
static bool running = false;
static uint16_t* frame_buffer = nullptr;
static const unsigned WIDTH = 320;
static const unsigned HEIGHT = 256;

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t env_cb;
static retro_log_printf_t log_cb = nullptr;
static bool pixel_format_set = false; // Flag to prevent re-setting pixel format

static void handle_input(void);
static void render_frame(void);
static bool decompress_jfd(const char* jfd_path, uint8_t** out_data, size_t* out_size);
static bool parse_jfd(uint8_t* data, size_t size, uint8_t** jfd_data, size_t* jfd_size);
static bool load_adf(const char* adf_path, uint8_t** out_data, size_t* out_size);

// Fallback logging
static void fallback_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// Send message to frontend
static void send_message(const char* msg) {
    if (env_cb) {
        struct retro_message retro_msg = {msg, 240};
        env_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &retro_msg);
    } else {
        fallback_log("Message: %s\n", msg);
    }
}

// Initialize logging
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
}

void retro_init(void) {
    log_message(RETRO_LOG_INFO, "retro_init called\n");

    frame_buffer = (uint16_t*)malloc(WIDTH * HEIGHT * sizeof(uint16_t));
    if (!frame_buffer) {
        log_message(RETRO_LOG_ERROR, "Failed to allocate frame buffer\n");
        send_message("Core failed to allocate frame buffer");
        running = false;
        return;
    }

    memset(frame_buffer, 0, WIDTH * HEIGHT * sizeof(uint16_t));
    for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
        frame_buffer[i] = 0xF800; // Red in RGB565
    }

    running = true;
    log_message(RETRO_LOG_INFO, "retro_init completed successfully\n");
    send_message("Acorn Archimedes Emulator initialized");
}

static bool decompress_jfd(const char* jfd_path, uint8_t** out_data, size_t* out_size) {
    if (!jfd_path) {
        log_message(RETRO_LOG_ERROR, "No .jfd path provided\n");
        return false;
    }
    log_message(RETRO_LOG_INFO, "Decompressing JFD file: %s\n", jfd_path);

    gzFile gz_file = gzopen(jfd_path, "rb");
    if (!gz_file) {
        log_message(RETRO_LOG_ERROR, "Failed to open gzip file: %s\n", jfd_path);
        return false;
    }

    // Allocate a buffer with a reasonable max size
    size_t buffer_size = ROM_SIZE; // Use ROM_SIZE as a sane limit
    *out_data = (uint8_t*)malloc(buffer_size);
    if (!*out_data) {
        log_message(RETRO_LOG_ERROR, "Failed to allocate decompression buffer\n");
        gzclose(gz_file);
        return false;
    }

    *out_size = 0;
    const size_t chunk_size = 8192; // Read in 8KB chunks
    while (*out_size < buffer_size) {
        int bytes_read = gzread(gz_file, *out_data + *out_size, chunk_size);
        if (bytes_read <= 0) {
            if (gzeof(gz_file)) {
                break; // End of file reached
            }
            int err;
            const char* err_str = gzerror(gz_file, &err);
            log_message(RETRO_LOG_ERROR, "Decompression error %d: %s\n", err, err_str);
            free(*out_data);
            *out_data = nullptr;
            *out_size = 0;
            gzclose(gz_file);
            return false;
        }
        *out_size += bytes_read;
    }

    if (*out_size >= buffer_size) {
        log_message(RETRO_LOG_ERROR, "Decompressed data exceeds ROM_SIZE (%u bytes)\n", ROM_SIZE);
        free(*out_data);
        *out_data = nullptr;
        *out_size = 0;
        gzclose(gz_file);
        return false;
    }

    gzclose(gz_file);
    log_message(RETRO_LOG_INFO, "Decompressed %zu bytes\n", *out_size);
    return true;
}

static bool parse_jfd(uint8_t* data, size_t size, uint8_t** jfd_data, size_t* jfd_size) {
    if (size < 48) {
        log_message(RETRO_LOG_ERROR, "Decompressed data too small for JFD header: %zu bytes\n", size);
        return false;
    }

    log_message(RETRO_LOG_DEBUG, "JFD header first 4 bytes: %02x %02x %02x %02x\n",
                data[0], data[1], data[2], data[3]);
    if (memcmp(data, "JFDI", 4) != 0) {
        log_message(RETRO_LOG_ERROR, "Invalid JFD identifier\n");
        return false;
    }

    uint32_t file_size = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    if (file_size > ROM_SIZE) {
        log_message(RETRO_LOG_WARN, "JFD size %u exceeds ROM_SIZE %u, capping\n", file_size, ROM_SIZE);
        file_size = ROM_SIZE;
    }

    uint32_t data_offset = data[32] | (data[33] << 8) | (data[34] << 16) | (data[35] << 24);
    if (data_offset < 48 || data_offset >= size) {
        log_message(RETRO_LOG_ERROR, "Invalid data table offset: %u\n", data_offset);
        return false;
    }

    size_t remaining_size = size - data_offset;
    if (remaining_size < file_size) {
        log_message(RETRO_LOG_ERROR, "Not enough data for JFD: need %u, have %zu\n", file_size, remaining_size);
        return false;
    }

    *jfd_data = (uint8_t*)malloc(file_size);
    if (!*jfd_data) {
        log_message(RETRO_LOG_ERROR, "Failed to allocate buffer for JFD data: %u bytes\n", file_size);
        return false;
    }

    memcpy(*jfd_data, data + data_offset, file_size);
    *jfd_size = file_size;
    log_message(RETRO_LOG_INFO, "Parsed JFD data: %zu bytes from offset %u\n", *jfd_size, data_offset);
    return true;
}

static bool load_adf(const char* adf_path, uint8_t** out_data, size_t* out_size) {
    if (!adf_path) {
        log_message(RETRO_LOG_ERROR, "No .adf path provided\n");
        return false;
    }
    log_message(RETRO_LOG_INFO, "Loading ADF file: %s\n", adf_path);

    FILE* file = fopen(adf_path, "rb");
    if (!file) {
        log_message(RETRO_LOG_ERROR, "Failed to open ADF file: %s\n", adf_path);
        return false;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Sanity check: typical ADF sizes are 800KB (819200 bytes) or 1.6MB (1638400 bytes)
    if (file_size != 819200 && file_size != 1638400) {
        log_message(RETRO_LOG_ERROR, "Unexpected ADF size: %zu bytes (expected 819200 or 1638400)\n", file_size);
        fclose(file);
        return false;
    }
    if (file_size > ROM_SIZE) {
        log_message(RETRO_LOG_WARN, "ADF size %zu exceeds ROM_SIZE %u, capping\n", file_size, ROM_SIZE);
        file_size = ROM_SIZE;
    }

    *out_data = (uint8_t*)malloc(file_size);
    if (!*out_data) {
        log_message(RETRO_LOG_ERROR, "Failed to allocate buffer for ADF data: %zu bytes\n", file_size);
        fclose(file);
        return false;
    }

    *out_size = fread(*out_data, 1, file_size, file);
    if (*out_size != file_size) {
        log_message(RETRO_LOG_ERROR, "Incomplete read: got %zu of %zu bytes\n", *out_size, file_size);
        free(*out_data);
        *out_data = nullptr;
        fclose(file);
        return false;
    }

    fclose(file);
    log_message(RETRO_LOG_INFO, "Loaded ADF data: %zu bytes\n", *out_size);
    return true;
}

bool retro_load_game(const struct retro_game_info* game) {
    log_message(RETRO_LOG_INFO, "retro_load_game called\n");

    const char* file_path = nullptr;
    if (game) {
        log_message(RETRO_LOG_DEBUG, "game pointer: %p\n", (void*)game);
        log_message(RETRO_LOG_DEBUG, "game->path: %p\n", (void*)game->path);
        if (game->path) {
            log_message(RETRO_LOG_DEBUG, "game->path: %s\n", game->path);
            log_message(RETRO_LOG_DEBUG, "game->path raw: ");
            for (int i = 0; i < 16 && game->path[i]; i++) {
                log_message(RETRO_LOG_DEBUG, "%02x ", (unsigned char)game->path[i]);
            }
            log_message(RETRO_LOG_DEBUG, "\n");
        } else {
            log_message(RETRO_LOG_WARN, "game->path is NULL, forcing fallback\n");
        }
    } else {
        log_message(RETRO_LOG_WARN, "game pointer is NULL, forcing fallback\n");
    }

    // Hardcode the path to gam.adf as a workaround
    static const char* hardcoded_path = "gam.adf";
    file_path = hardcoded_path;
    log_message(RETRO_LOG_INFO, "Forcing path to: %s\n", file_path);
    log_message(RETRO_LOG_DEBUG, "file_path raw: ");
    for (int i = 0; i < 8; i++) {
        log_message(RETRO_LOG_DEBUG, "%02x ", (unsigned char)file_path[i]);
    }
    log_message(RETRO_LOG_DEBUG, "\n");

    log_message(RETRO_LOG_INFO, "Loading file from: %s\n", file_path);

    // Check file extension
    const char* ext = strrchr(file_path, '.');
    if (!ext) {
        log_message(RETRO_LOG_ERROR, "No file extension found in path: %s\n", file_path);
        return false;
    }

    bool is_jfd = (strcasecmp(ext, ".jfd") == 0);
    bool is_adf = (strcasecmp(ext, ".adf") == 0);

    if (!is_jfd && !is_adf) {
        log_message(RETRO_LOG_ERROR, "Unsupported file extension: %s\n", ext);
        return false;
    }

    uint8_t* file_data = nullptr;
    size_t file_size = 0;

    if (is_jfd) {
        uint8_t* decompressed_data = nullptr;
        size_t decompressed_size = 0;
        if (!decompress_jfd(file_path, &decompressed_data, &decompressed_size)) {
            log_message(RETRO_LOG_ERROR, "Failed to decompress JFD file\n");
            send_message("Failed to decompress JFD file");
            return false;
        }

        if (!parse_jfd(decompressed_data, decompressed_size, &file_data, &file_size)) {
            log_message(RETRO_LOG_ERROR, "Failed to parse JFD data\n");
            send_message("Failed to parse JFD data");
            free(decompressed_data);
            return false;
        }
        free(decompressed_data); // No longer needed
    } else if (is_adf) {
        if (!load_adf(file_path, &file_data, &file_size)) {
            log_message(RETRO_LOG_ERROR, "Failed to load ADF file\n");
            send_message("Failed to load ADF file");
            return false;
        }
    }

    free(file_data); // memory_create will reload it

    memory = memory_create(file_path);
    if (!memory) {
        log_message(RETRO_LOG_ERROR, "Failed to create memory system\n");
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

    log_message(RETRO_LOG_INFO, "Successfully loaded file\n");
    send_message("Successfully loaded file");
    return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info) {
    log_message(RETRO_LOG_WARN, "retro_load_game_special not implemented: game_type=%u, num_info=%zu\n", game_type, num_info);
    send_message("Special game loading not supported");
    return false;
}

void retro_deinit(void) {
    log_message(RETRO_LOG_INFO, "retro_deinit called\n");
    running = false;
    if (cpu) { cpu_destroy(cpu); cpu = nullptr; }
    if (memory) { memory_destroy(memory); memory = nullptr; }
    if (frame_buffer) { free(frame_buffer); frame_buffer = nullptr; }
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
    info->library_name = "Acorn Archimedes Emulator";
    info->library_version = "1.0";
    info->valid_extensions = "jfd|adf";
    info->need_fullpath = true;
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
        uint32_t pc = cpu->registers[15] & 0x03FFFFFF;
        if (pc > 0x03600000) {
            log_message(RETRO_LOG_ERROR, "PC out of bounds: %08x at step %u\n", cpu->registers[15], i);
            running = false;
            break;
        }
        // Removed cpu_step logging as requested
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
            frame_buffer[y * WIDTH + x] = (uint16_t)((x + y + (clock() / 1000)) & 0xFFFF);
        }
    }
}