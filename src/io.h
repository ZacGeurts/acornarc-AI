#ifndef IO_H
#define IO_H

#include <cstdint>

// Forward declaration of struct memory (to avoid circular dependency with memory.h)
struct memory;

typedef struct io {
    uint32_t memc_control; // Existing member
    uint32_t vidc_control; // Added for VIDC writes
} io_t;

io_t* io_create(void);
void io_destroy(io_t* io);
uint32_t io_read_word(io_t* io, struct memory* mem, uint32_t address);
void io_write_word(io_t* io, struct memory* mem, uint32_t address, uint32_t value);
uint8_t io_read_byte(io_t* io, struct memory* mem, uint32_t address);
void io_write_byte(io_t* io, struct memory* mem, uint32_t address, uint8_t value);

#endif