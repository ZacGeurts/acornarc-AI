#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>
#include <cstddef> // Added for size_t

// Forward declaration of struct io (to avoid circular dependency with io.h)
struct io;

#define RAM_SIZE (static_cast<size_t>(16 * 1024 * 1024)) // 16MB
#define ROM_SIZE (static_cast<size_t>(2 * 1024 * 1024))  // 2MB
#define RAM_BASE 0x00000000
#define ROM_DEFAULT_BASE 0x03800000
#define IO_BASE 0x02000000
#define IO_SIZE 0x02000000
#define ADDR_MASK 0x03FFFFFF // 26-bit address space

typedef struct memory {
    uint8_t* ram;
    uint8_t* rom;
    size_t rom_size;
    uint32_t rom_base;
    uint32_t floppy_offset;
    struct io* io;
    int is_boot_mode; // 1 at boot, 0 after initialization
} memory_t;

memory_t* memory_create(const char* jfd_path, uint32_t rom_base, struct io* io);
void memory_destroy(memory_t* mem);
uint32_t memory_read_word(memory_t* mem, uint32_t address);
void memory_write_word(memory_t* mem, uint32_t address, uint32_t value);
uint8_t memory_read_byte(memory_t* mem, uint32_t address);
void memory_write_byte(memory_t* mem, uint32_t address, uint8_t value);

#endif