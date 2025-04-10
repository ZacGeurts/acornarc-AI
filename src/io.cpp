#include "io.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libretro.h>

// Use RAM_BASE from memory.h, no need to redefine
// #define RAM_BASE 0x00000000 // Removed, already in memory.h

io_t* io_create(uint32_t width, uint32_t height) {
    io_t* io = (io_t*)malloc(sizeof(io_t));
    if (!io) {
        printf("Failed to allocate I/O struct\n");
        return NULL;
    }

    // Initialize VIDC
    io->vidc.control = 0;
    memset(io->vidc.palette, 0, sizeof(io->vidc.palette));
    io->vidc.palette[0] = 0x000; // Black
    io->vidc.palette[1] = 0xF00; // Red
    io->vidc.palette[2] = 0x0F0; // Green
    io->vidc.palette[3] = 0x00F; // Blue
    io->vidc.palette[4] = 0xFFF; // White
    io->vidc.border_color = 0;
    io->vidc.cursor_palette[0] = 0xFFF; // White cursor default
    io->vidc.cursor_palette[1] = 0xF00; // Red
    io->vidc.cursor_palette[2] = 0x000; // Black
    io->vidc.h_cycle = 832;         // Default: 832 cycles (e.g., 640 + borders)
    io->vidc.h_sync_width = 64;
    io->vidc.h_border_start = 64;
    io->vidc.h_display_start = 128;
    io->vidc.h_display_end = 768;   // 640 pixels display
    io->vidc.h_border_end = 768;
    io->vidc.h_cursor_start = 0;
    io->vidc.v_cycle = 625;         // Default: 625 lines (e.g., 480 + borders)
    io->vidc.v_sync_width = 2;
    io->vidc.v_border_start = 35;
    io->vidc.v_display_start = 70;
    io->vidc.v_display_end = 550;   // 480 lines display
    io->vidc.v_border_end = 590;
    io->vidc.v_cursor_end = 0;
    io->vidc.sound_freq = 24;       // Default 24 µs (~41.67 kHz)
    io->vidc.sound_control = 0;
    io->vidc.video_base = 0x00000000;
    io->vidc.ext_latch_c = 0;       // Default 24 MHz, +ve sync

    // Initialize IOC
    io->ioc.control = 0;
    io->ioc.timer0_low = 0;
    io->ioc.timer0_high = 0;
    io->ioc.timer1_low = 0;
    io->ioc.timer1_high = 0;
    io->ioc.timer0_latch = 0xFFFF;  // Default full count
    io->ioc.timer1_latch = 0xFFFF;
    io->ioc.irq_status_a = 0;
    io->ioc.irq_request_a = 0;
    io->ioc.irq_mask_a = (1 << 5) | (1 << 6); // Timer0, Timer1 enabled
    io->ioc.irq_status_b = 0;
    io->ioc.irq_request_b = 0;
    io->ioc.irq_mask_b = 0;
    io->ioc.fiq_status = 0;
    io->ioc.fiq_request = 0;
    io->ioc.fiq_mask = 0;
    io->ioc.podule_irq_mask = 0;
    io->ioc.podule_irq_request = 0;

    // Frame buffer
    io->frame_width = width;
    io->frame_height = height;
    io->frame_buffer = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!io->frame_buffer) {
        printf("Failed to allocate frame buffer\n");
        free(io);
        return NULL;
    }
    memset(io->frame_buffer, 0, width * height * sizeof(uint32_t));

    // Interrupts and timing
    io->irq_pending = false;
    io->fiq_pending = false;
    io->cycles = 0;

    printf("I/O module initialized\n");
    return io;
}

void io_destroy(io_t* io) {
    if (io) {
        if (io->frame_buffer) free(io->frame_buffer);
        free(io);
    }
}

uint32_t io_read_word(io_t* io, memory_t* mem, uint32_t address) {
    if (address >= VIDC_BASE && address < VIDC_BASE + VIDC_SIZE) {
        uint32_t offset = (address - VIDC_BASE) >> 2;
        switch (offset) {
            case 0:
                // Simulate VIDC status: reflect VFLY interrupt state
                return io->vidc.control | (io->ioc.irq_request_a & (1 << 3) ? 0x8 : 0);
            case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
            case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
            case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24:
            case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
            case 33: case 34: case 35: case 36: case 37: case 38: case 39: case 40:
            case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48:
            case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56:
            case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64:
            case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72:
            case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 80:
            case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 88:
            case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96:
            case 97: case 98: case 99: case 100: case 101: case 102: case 103: case 104:
            case 105: case 106: case 107: case 108: case 109: case 110: case 111: case 112:
            case 113: case 114: case 115: case 116: case 117: case 118: case 119: case 120:
            case 121: case 122: case 123: case 124: case 125: case 126: case 127: case 128:
            case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136:
            case 137: case 138: case 139: case 140: case 141: case 142: case 143: case 144:
            case 145: case 146: case 147: case 148: case 149: case 150: case 151: case 152:
            case 153: case 154: case 155: case 156: case 157: case 158: case 159: case 160:
            case 161: case 162: case 163: case 164: case 165: case 166: case 167: case 168:
            case 169: case 170: case 171: case 172: case 173: case 174: case 175: case 176:
            case 177: case 178: case 179: case 180: case 181: case 182: case 183: case 184:
            case 185: case 186: case 187: case 188: case 189: case 190: case 191: case 192:
            case 193: case 194: case 195: case 196: case 197: case 198: case 199: case 200:
            case 201: case 202: case 203: case 204: case 205: case 206: case 207: case 208:
            case 209: case 210: case 211: case 212: case 213: case 214: case 215: case 216:
            case 217: case 218: case 219: case 220: case 221: case 222: case 223: case 224:
            case 225: case 226: case 227: case 228: case 229: case 230: case 231: case 232:
            case 233: case 234: case 235: case 236: case 237: case 238: case 239: case 240:
            case 241: case 242: case 243: case 244: case 245: case 246: case 247: case 248:
            case 249: case 250: case 251: case 252: case 253: case 254: case 255:
                return io->vidc.palette[offset - 1];
            case 256: return io->vidc.border_color;
            case 257: case 258: case 259: return io->vidc.cursor_palette[offset - 257];
            case 260: return io->vidc.h_cycle;
            case 261: return io->vidc.h_sync_width;
            case 262: return io->vidc.h_border_start;
            case 263: return io->vidc.h_display_start;
            case 264: return io->vidc.h_display_end;
            case 265: return io->vidc.h_border_end;
            case 266: return io->vidc.h_cursor_start;
            case 267: return io->vidc.v_cycle;
            case 268: return io->vidc.v_sync_width;
            case 269: return io->vidc.v_border_start;
            case 270: return io->vidc.v_display_start;
            case 271: return io->vidc.v_display_end;
            case 272: return io->vidc.v_border_end;
            case 273: return io->vidc.v_cursor_end;
            case 274: return io->vidc.sound_freq;
            case 275: return io->vidc.sound_control;
            case 276: return io->vidc.video_base;
            case 277: return io->vidc.ext_latch_c;
            default:
                printf("VIDC read at 0x%08X (offset 0x%08X) (unimplemented)\n", address, offset);
                return 0;
        }
    } else if (address >= IOC_BASE && address < IOC_BASE + IOC_SIZE) {
        uint32_t offset = (address - IOC_BASE) >> 2;
        switch (offset) {
            case 0: return io->ioc.control;
            case 1: return io->ioc.timer0_low;
            case 2: return io->ioc.timer0_high;
            case 3: return io->ioc.timer1_low; // Fixed typo: was timer1_cklow
            case 4: return io->ioc.timer1_high;
            case 5: return io->ioc.timer0_latch;
            case 6: return io->ioc.timer1_latch;
            case 7: return io->ioc.irq_status_a;
            case 8: return io->ioc.irq_request_a;
            case 9: return io->ioc.irq_mask_a;
            case 10: return io->ioc.irq_status_b;
            case 11: return io->ioc.irq_request_b;
            case 12: return io->ioc.irq_mask_b;
            case 13: return io->ioc.fiq_status;
            case 14: return io->ioc.fiq_request;
            case 15: return io->ioc.fiq_mask;
            case 16: return io->ioc.podule_irq_mask;
            case 17: return io->ioc.podule_irq_request;
            default:
                printf("IOC read at 0x%08X (offset 0x%08X) (unimplemented)\n", address, offset);
                return 0;
        }
    }
    printf("I/O read at 0x%08X (unimplemented)\n", address);
    return 0;
}

void io_write_word(io_t* io, memory_t* mem, uint32_t address, uint32_t value) {
    static uint32_t last_logged_address = 0xFFFFFFFF;
    static uint32_t last_logged_value = 0xFFFFFFFF;
    static int log_counter = 0;

    if (address >= VIDC_BASE && address < VIDC_BASE + VIDC_SIZE) {
        uint32_t offset = (address - VIDC_BASE) >> 2;
        switch (offset) {
            case 0:
                io->vidc.control = value;
                if (address != last_logged_address || value != last_logged_value || log_counter % 1000 == 0) {
                    printf("VIDC control write: 0x%08X at 0x%08X\n", value, address);
                    last_logged_address = address;
                    last_logged_value = value;
                }
                log_counter++;
                break;
            case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
            case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
            case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24:
            case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
            case 33: case 34: case 35: case 36: case 37: case 38: case 39: case 40:
            case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48:
            case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 56:
            case 57: case 58: case 59: case 60: case 61: case 62: case 63: case 64:
            case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72:
            case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 80:
            case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 88:
            case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96:
            case 97: case 98: case 99: case 100: case 101: case 102: case 103: case 104:
            case 105: case 106: case 107: case 108: case 109: case 110: case 111: case 112:
            case 113: case 114: case 115: case 116: case 117: case 118: case 119: case 120:
            case 121: case 122: case 123: case 124: case 125: case 126: case 127: case 128:
            case 129: case 130: case 131: case 132: case 133: case 134: case 135: case 136:
            case 137: case 138: case 139: case 140: case 141: case 142: case 143: case 144:
            case 145: case 146: case 147: case 148: case 149: case 150: case 151: case 152:
            case 153: case 154: case 155: case 156: case 157: case 158: case 159: case 160:
            case 161: case 162: case 163: case 164: case 165: case 166: case 167: case 168:
            case 169: case 170: case 171: case 172: case 173: case 174: case 175: case 176:
            case 177: case 178: case 179: case 180: case 181: case 182: case 183: case 184:
            case 185: case 186: case 187: case 188: case 189: case 190: case 191: case 192:
            case 193: case 194: case 195: case 196: case 197: case 198: case 199: case 200:
            case 201: case 202: case 203: case 204: case 205: case 206: case 207: case 208:
            case 209: case 210: case 211: case 212: case 213: case 214: case 215: case 216:
            case 217: case 218: case 219: case 220: case 221: case 222: case 223: case 224:
            case 225: case 226: case 227: case 228: case 229: case 230: case 231: case 232:
            case 233: case 234: case 235: case 236: case 237: case 238: case 239: case 240:
            case 241: case 242: case 243: case 244: case 245: case 246: case 247: case 248:
            case 249: case 250: case 251: case 252: case 253: case 254: case 255:
                io->vidc.palette[offset - 1] = value & 0x1FFF; // 13-bit RGB
                printf("VIDC palette[%u] write: 0x%04X at 0x%08X\n", offset - 1, value & 0x1FFF, address);
                break;
            case 256:
                io->vidc.border_color = value & 0x1FFF;
                printf("VIDC border_color write: 0x%04X at 0x%08X\n", value & 0x1FFF, address);
                break;
            case 257: case 258: case 259:
                io->vidc.cursor_palette[offset - 257] = value & 0x1FFF;
                printf("VIDC cursor_palette[%u] write: 0x%04X at 0x%08X\n", offset - 257, value & 0x1FFF, address);
                break;
            case 260: io->vidc.h_cycle = value; break;
            case 261: io->vidc.h_sync_width = value; break;
            case 262: io->vidc.h_border_start = value; break;
            case 263:
                io->vidc.h_display_start = value;
                io->frame_width = io->vidc.h_display_end - io->vidc.h_display_start;
                break;
            case 264:
                io->vidc.h_display_end = value;
                io->frame_width = io->vidc.h_display_end - io->vidc.h_display_start;
                break;
            case 265: io->vidc.h_border_end = value; break;
            case 266: io->vidc.h_cursor_start = value; break;
            case 267: io->vidc.v_cycle = value; break;
            case 268: io->vidc.v_sync_width = value; break;
            case 269: io->vidc.v_border_start = value; break;
            case 270:
                io->vidc.v_display_start = value;
                io->frame_height = io->vidc.v_display_end - io->vidc.v_display_start;
                break;
            case 271:
                io->vidc.v_display_end = value;
                io->frame_height = io->vidc.v_display_end - io->vidc.v_display_start;
                break;
            case 272: io->vidc.v_border_end = value; break;
            case 273: io->vidc.v_cursor_end = value; break;
            case 274:
                io->vidc.sound_freq = value & 0xFF; // 8-bit
                printf("VIDC sound_freq write: %u at 0x%08X\n", value & 0xFF, address);
                break;
            case 275: io->vidc.sound_control = value; break;
            case 276:
                io->vidc.video_base = value & ADDR_MASK;
                printf("VIDC video_base write: 0x%08X at 0x%08X\n", value, address);
                break;
            case 277:
                io->vidc.ext_latch_c = value & 0xFF; // Clock and sync bits
                printf("VIDC ext_latch_c write: 0x%02X at 0x%08X\n", value & 0xFF, address);
                break;
            default:
                printf("VIDC write at 0x%08X (offset 0x%08X) with value 0x%08X (unimplemented)\n", address, offset, value);
                break;
        }
    } else if (address >= IOC_BASE && address < IOC_BASE + IOC_SIZE) {
        uint32_t offset = (address - IOC_BASE) >> 2;
        switch (offset) {
            case 0: io->ioc.control = value; break;
            case 1: io->ioc.timer0_low = value & 0xFFFF; break;
            case 2: io->ioc.timer0_high = value & 0xFFFF; break;
            case 3: io->ioc.timer1_low = value & 0xFFFF; break;
            case 4: io->ioc.timer1_high = value & 0xFFFF; break;
            case 5:
                io->ioc.timer0_latch = value & 0xFFFF;
                io->ioc.timer0_low = value & 0xFFFF; // Reset on latch write
                break;
            case 6:
                io->ioc.timer1_latch = value & 0xFFFF;
                io->ioc.timer1_low = value & 0xFFFF;
                break;
            case 7: io->ioc.irq_status_a = value; break;
            case 8: io->ioc.irq_request_a = value; break;
            case 9: io->ioc.irq_mask_a = value; break;
            case 10: io->ioc.irq_status_b = value; break;
            case 11: io->ioc.irq_request_b = value; break;
            case 12: io->ioc.irq_mask_b = value; break;
            case 13: io->ioc.fiq_status = value; break;
            case 14: io->ioc.fiq_request = value; break;
            case 15: io->ioc.fiq_mask = value; break;
            case 16: io->ioc.podule_irq_mask = value; break;
            case 17: io->ioc.podule_irq_request = value; break;
            default:
                printf("IOC write at 0x%08X (offset 0x%08X) with value 0x%08X (unimplemented)\n", address, offset, value);
                break;
        }
    } else if (address == 0x02FF5500) {
        // Temporary workaround: treat as VIDC control write
        io->vidc.control = value;
        printf("Mapped write to VIDC control at 0x%08X with value 0x%08X\n", address, value);
    } else {
        printf("I/O write at 0x%08X with value 0x%08X (unimplemented)\n", address, value);
    }
}

uint8_t io_read_byte(io_t* io, memory_t* mem, uint32_t address) {
    uint32_t word = io_read_word(io, mem, address & ~3);
    return (word >> ((address & 3) * 8)) & 0xFF;
}

void io_write_byte(io_t* io, memory_t* mem, uint32_t address, uint8_t value) {
    uint32_t word_addr = address & ~3;
    uint32_t shift = (address & 3) * 8;
    uint32_t word = io_read_word(io, mem, word_addr);
    word = (word & ~(0xFF << shift)) | (value << shift);
    io_write_word(io, mem, word_addr, word);
}

void io_render_frame(io_t* io, memory_t* mem, retro_video_refresh_t video_cb) {
    if (!io->frame_buffer || !video_cb) return;

    uint16_t* rgb565_buffer = (uint16_t*)malloc(io->frame_width * io->frame_height * sizeof(uint16_t));
    if (!rgb565_buffer) {
        printf("Failed to allocate RGB565 buffer\n");
        return;
    }

    // Assume 8 bits per pixel (256 colors) for simplicity, adjust based on VIDC control
    uint8_t* video_mem = mem->ram + (io->vidc.video_base - RAM_BASE);
    uint32_t display_width = io->vidc.h_display_end - io->vidc.h_display_start;
    uint32_t display_height = io->vidc.v_display_end - io->vidc.v_display_start;

    for (uint32_t y = 0; y < io->frame_height && y < display_height; y++) {
        for (uint32_t x = 0; x < io->frame_width && x < display_width; x++) {
            uint32_t pixel_idx = y * io->frame_width + x;
            uint8_t pixel = video_mem[pixel_idx];
            uint32_t rgb = io->vidc.palette[pixel & 0xFF]; // 13-bit RGB

            uint8_t r = ((rgb >> 9) & 0xF) * 255 / 15; // 4-bit R
            uint8_t g = ((rgb >> 4) & 0x1F) * 255 / 31; // 5-bit G
            uint8_t b = (rgb & 0xF) * 255 / 15;        // 4-bit B
            io->frame_buffer[pixel_idx] = (0xFF << 24) | (r << 16) | (g << 8) | b;

            uint16_t r5 = (r >> 3) & 0x1F;
            uint16_t g6 = (g >> 2) & 0x3F;
            uint16_t b5 = (b >> 3) & 0x1F;
            rgb565_buffer[pixel_idx] = (r5 << 11) | (g6 << 5) | b5;
        }
    }

    video_cb(rgb565_buffer, io->frame_width, io->frame_height, io->frame_width * sizeof(uint16_t));
    free(rgb565_buffer);

    // Trigger VFLY interrupt
    io->ioc.irq_request_a |= (1 << 3); // Vertical Flyback
}

void io_update_timers(io_t* io) {
    const uint32_t cycles_per_frame = 8000000 / 50; // 8 MHz / 50 Hz
    io->cycles += cycles_per_frame;

    // Update timers (16-bit, wrap at latch value)
    io->ioc.timer0_low += cycles_per_frame;
    if (io->ioc.timer0_low >= io->ioc.timer0_latch) {
        io->ioc.timer0_low -= io->ioc.timer0_latch;
        io->ioc.timer0_high++;
        io->ioc.irq_request_a |= (1 << 5); // Timer 0 IRQ
    }

    io->ioc.timer1_low += cycles_per_frame;
    if (io->ioc.timer1_low >= io->ioc.timer1_latch) {
        io->ioc.timer1_low -= io->ioc.timer1_latch;
        io->ioc.timer1_high++;
        io->ioc.irq_request_a |= (1 << 6); // Timer 1 IRQ
    }

    // Simulate VFLY every frame
    if ((io->cycles % cycles_per_frame) == 0) {
        io->ioc.irq_request_a |= (1 << 3); // VFLY interrupt
    }

    // Update interrupt flags
    io->irq_pending = ((io->ioc.irq_request_a & io->ioc.irq_mask_a) ||
                       (io->ioc.irq_request_b & io->ioc.irq_mask_b)) != 0;
    io->fiq_pending = (io->ioc.fiq_request & io->ioc.fiq_mask) != 0;
}

bool io_get_irq(io_t* io) {
    return io->irq_pending;
}

bool io_get_fiq(io_t* io) {
    return io->fiq_pending;
}