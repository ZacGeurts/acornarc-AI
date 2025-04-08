#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>

#define RAM_BASE    0x00000000
#define RAM_SIZE    (8 * 1024 * 1024) // 8MB
#define IO_BASE     0x03200000
#define IO_SIZE     (2 * 1024 * 1024) // 2MB
#define ROM_BASE    0x03400000
#define ROM_SIZE    (512 * 1024)      // 512KB for now
#define ADDR_MASK   0x03FFFFFF        // 26-bit address space

typedef struct memory memory_t;

memory_t* memory_create(const char* jfd_path); // Pass JFD file path
void memory_destroy(memory_t* mem);
uint32_t memory_read_word(memory_t* mem, uint32_t address);
void memory_write_word(memory_t* mem, uint32_t address, uint32_t value);

#endif