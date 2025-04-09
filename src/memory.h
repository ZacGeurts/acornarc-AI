#ifndef MEMORY_H
#define MEMORY_H

#include <cstdint>
#include <cstddef> // Added for size_t

#define RAM_BASE    0x00000000
#define RAM_SIZE    (4 * 1024 * 1024) // 4MB (ARM3 A400 series standard)
#define IO_BASE     0x03200000
#define IO_SIZE     (2 * 1024 * 1024) // 2MB
#define ROM_BASE    0x03400000
#define ROM_SIZE    (512 * 1024)      // 512KB (RISC OS 3.1x ROM size)
#define ADDR_MASK   0x03FFFFFF        // 26-bit address space

typedef struct memory memory_t;

extern uint8_t* floppy_data;
extern size_t floppy_size;

memory_t* memory_create(const char* jfd_path);
void memory_destroy(memory_t* mem);
uint32_t memory_read_word(memory_t* mem, uint32_t address);
void memory_write_word(memory_t* mem, uint32_t address, uint32_t value);
uint8_t memory_read_byte(memory_t* mem, uint32_t address);
void memory_write_byte(memory_t* mem, uint32_t address, uint8_t value);

#endif