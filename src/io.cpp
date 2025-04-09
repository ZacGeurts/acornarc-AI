#include "io.h"
#include "memory.h" // Include memory.h to get the full definition of memory_t
#include <stdio.h>
#include <stdlib.h>

io_t* io_create(void) {
    io_t* io = (io_t*)malloc(sizeof(io_t));
    if (!io) {
        printf("Failed to allocate I/O struct\n");
        return NULL;
    }
    io->vidc_control = 0;
    printf("I/O module initialized\n");
    return io;
}

void io_destroy(io_t* io) {
    if (io) {
        free(io);
    }
}

uint32_t io_read_word(io_t* io, memory_t* mem, uint32_t address) {
    printf("I/O read at 0x%08X (unimplemented)\n", address);
    return 0; // Return 0 for now
}

void io_write_word(io_t* io, memory_t* mem, uint32_t address, uint32_t value) {
    if (address >= 0x03400000 && address < 0x03600000) { // VIDC region
        printf("VIDC write at 0x%08X with value 0x%08X\n", address, value);
        io->vidc_control = value; // Store the value (e.g., for VIDC control)
    } else {
        printf("I/O write at 0x%08X with value 0x%08X (unimplemented)\n", address, value);
    }
}

uint8_t io_read_byte(io_t* io, memory_t* mem, uint32_t address) {
    printf("I/O byte read at 0x%08X (unimplemented)\n", address);
    return 0;
}

void io_write_byte(io_t* io, memory_t* mem, uint32_t address, uint8_t value) {
    printf("I/O byte write at 0x%08X with value 0x%02X (unimplemented)\n", address, value);
}