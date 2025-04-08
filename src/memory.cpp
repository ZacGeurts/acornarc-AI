#include "memory.h"
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct memory {
    uint8_t* ram;    // Main RAM
    uint8_t* rom;    // ROM (loaded from JFD)
    size_t rom_size; // Actual size of loaded ROM
};

memory_t* memory_create(const char* jfd_path) {
    memory_t* mem = (memory_t*)malloc(sizeof(memory_t));
    mem->ram = (uint8_t*)calloc(RAM_SIZE, 1); // Zero-initialized RAM
    mem->rom = (uint8_t*)calloc(ROM_SIZE, 1); // Zero-initialized ROM
    mem->rom_size = 0;

    // Load JFD file into ROM
    if (jfd_path) {
        FILE* file = fopen(jfd_path, "rb");
        if (!file) {
            printf("Failed to open JFD file: %s\n", jfd_path);
        } else {
            fseek(file, 0, SEEK_END);
            size_t file_size = ftell(file);
            rewind(file);

            // Cap at ROM_SIZE
            mem->rom_size = (file_size > ROM_SIZE) ? ROM_SIZE : file_size;
            size_t read = fread(mem->rom, 1, mem->rom_size, file);
            if (read != mem->rom_size) {
                printf("Warning: Incomplete JFD read (%zu of %zu bytes)\n", read, mem->rom_size);
            } else {
                printf("Loaded JFD: %zu bytes into ROM at 0x%08X\n", mem->rom_size, ROM_BASE);
            }
            fclose(file);
        }
    }

    return mem;
}

void memory_destroy(memory_t* mem) {
    free(mem->ram);
    free(mem->rom);
    free(mem);
}

uint32_t memory_read_word(memory_t* mem, uint32_t address) {
    address &= ADDR_MASK; // 26-bit mask

    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t* ptr = (uint32_t*)(mem->ram + (address - RAM_BASE));
        return *ptr; // Assumes little-endian (ARM2 is little-endian)
    }
    else if (address >= ROM_BASE && address < ROM_BASE + mem->rom_size - 3) {
        uint32_t* ptr = (uint32_t*)(mem->rom + (address - ROM_BASE));
        return *ptr;
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        printf("I/O read at 0x%08X (unimplemented)\n", address);
        return 0;
    }
    else {
        printf("Invalid read at 0x%08X\n", address);
        return 0;
    }
}

void memory_write_word(memory_t* mem, uint32_t address, uint32_t value) {
    address &= ADDR_MASK; // 26-bit mask

    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t* ptr = (uint32_t*)(mem->ram + (address - RAM_BASE));
        *ptr = value;
    }
    else if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE) {
        printf("Attempted write to ROM at 0x%08X ignored\n", address);
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        printf("I/O write at 0x%08X = 0x%08X (unimplemented)\n", address, value);
    }
    else {
        printf("Invalid write at 0x%08X = 0x%08X\n", address, value);
    }
}