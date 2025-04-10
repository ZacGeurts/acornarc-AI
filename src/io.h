#ifndef IO_H
#define IO_H

#include <cstdint>
#include <libretro.h> // For retro_video_refresh_t

// Forward declaration of struct memory
struct memory;

// Memory-mapped base addresses and sizes
#define VIDC_BASE 0x03400000
#define VIDC_SIZE 0x00200000
#define IOC_BASE  0x03200000
#define IOC_SIZE  0x00200000

// VIDC registers
typedef struct {
    uint32_t control;          // VIDC control register (simplified)
    uint32_t palette[256];     // 256 palette entries (13-bit RGB: 4R, 5G, 4B)
    uint32_t border_color;     // Border color (13-bit RGB)
    uint32_t cursor_palette[3]; // Cursor palette (3 colors, 13-bit RGB)
    uint32_t h_cycle;          // Horizontal total cycle
    uint32_t h_sync_width;     // Horizontal sync width
    uint32_t h_border_start;   // Horizontal border start
    uint32_t h_display_start;  // Horizontal display start
    uint32_t h_display_end;    // Horizontal display end
    uint32_t h_border_end;     // Horizontal border end
    uint32_t h_cursor_start;   // Horizontal cursor start
    uint32_t v_cycle;          // Vertical total cycle
    uint32_t v_sync_width;     // Vertical sync width
    uint32_t v_border_start;   // Vertical border start
    uint32_t v_display_start;  // Vertical display start
    uint32_t v_display_end;    // Vertical display end
    uint32_t v_border_end;     // Vertical border end
    uint32_t v_cursor_end;     // Vertical cursor end (end of cursor data)
    uint32_t sound_freq;       // Sound frequency (8-bit, 3-255 µs intervals)
    uint32_t sound_control;    // Sound control (e.g., stereo positions)
    uint32_t video_base;       // Video memory base address
    uint32_t ext_latch_c;      // External Latch C (clock speed, sync polarity)
} vidc_t;

// IOC registers
typedef struct {
    uint32_t control;          // Control register (general I/O control)
    uint32_t timer0_low;       // Timer 0 low word
    uint32_t timer0_high;      // Timer 0 high word (latched)
    uint32_t timer1_low;       // Timer 1 low word
    uint32_t timer1_high;      // Timer 1 high word (latched)
    uint32_t timer0_latch;     // Timer 0 latch value
    uint32_t timer1_latch;     // Timer 1 latch value
    uint32_t irq_status_a;     // IRQ Status A (e.g., timers, VFLY)
    uint32_t irq_request_a;    // IRQ Request A
    uint32_t irq_mask_a;       // IRQ Mask A
    uint32_t irq_status_b;     // IRQ Status B (e.g., sound, podule)
    uint32_t irq_request_b;    // IRQ Request B
    uint32_t irq_mask_b;       // IRQ Mask B
    uint32_t fiq_status;       // FIQ Status (e.g., floppy, Econet)
    uint32_t fiq_request;      // FIQ Request
    uint32_t fiq_mask;         // FIQ Mask
    uint32_t podule_irq_mask;  // Podule IRQ mask
    uint32_t podule_irq_request; // Podule IRQ request
} ioc_t;

typedef struct io {
    uint32_t memc_control;     // MEMC control (existing)
    vidc_t vidc;               // VIDC state
    ioc_t ioc;                 // IOC state
    uint32_t* frame_buffer;    // Frame buffer for video output
    uint32_t frame_width;      // Frame buffer width
    uint32_t frame_height;     // Frame buffer height
    bool irq_pending;          // IRQ pending flag
    bool fiq_pending;          // FIQ pending flag
    uint64_t cycles;           // Cycle counter for timing
} io_t;

// Function declarations
io_t* io_create(uint32_t width, uint32_t height);
void io_destroy(io_t* io);
uint32_t io_read_word(io_t* io, struct memory* mem, uint32_t address);
void io_write_word(io_t* io, struct memory* mem, uint32_t address, uint32_t value);
uint8_t io_read_byte(io_t* io, struct memory* mem, uint32_t address);
void io_write_byte(io_t* io, struct memory* mem, uint32_t address, uint8_t value);
void io_render_frame(io_t* io, struct memory* mem, retro_video_refresh_t video_cb);
void io_update_timers(io_t* io); // Update timers and interrupts
bool io_get_irq(io_t* io);       // Get IRQ status
bool io_get_fiq(io_t* io);       // Get FIQ status

#endif