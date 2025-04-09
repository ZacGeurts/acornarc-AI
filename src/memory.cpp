#include "memory.h"
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct memory {
    uint8_t* ram;    
    uint8_t* rom;    
    size_t rom_size; 
    uint32_t floppy_offset; 
};

memory_t* memory_create(const char* jfd_path) {
    memory_t* mem = (memory_t*)malloc(sizeof(memory_t));
    if (!mem) {
        printf("Failed to allocate memory struct\n");
        return nullptr;
    }

    mem->ram = (uint8_t*)calloc(RAM_SIZE, 1); 
    mem->rom = (uint8_t*)calloc(ROM_SIZE, 1); 
    mem->rom_size = 0;
    mem->floppy_offset = 0;

    if (!mem->ram || !mem->rom) {
        printf("Failed to allocate RAM or ROM\n");
        free(mem->ram);
        free(mem->rom);
        free(mem);
        return nullptr;
    }

    if (jfd_path) {
        FILE* file = fopen(jfd_path, "rb");
        if (!file) {
            printf("Failed to open file: %s\n", jfd_path);
        } else {
            fseek(file, 0, SEEK_END);
            size_t file_size = ftell(file);
            rewind(file);

            mem->rom_size = (file_size > ROM_SIZE) ? ROM_SIZE : file_size;
            size_t read = fread(mem->rom, 1, mem->rom_size, file);
            if (read != mem->rom_size) {
                printf("Warning: Incomplete read (%zu of %zu bytes)\n", read, mem->rom_size);
            } else {
                printf("Loaded ROM: %zu bytes into ROM at 0x%08X\n", mem->rom_size, ROM_BASE);
            }
            fclose(file);
        }
    }

    return mem;
}

void memory_destroy(memory_t* mem) {
    if (mem) {
        free(mem->ram);
        free(mem->rom);
        free(mem);
    }
}

uint32_t memory_read_word(memory_t* mem, uint32_t address) {
    address &= ADDR_MASK;
    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t* ptr = (uint32_t*)(mem->ram + (address - RAM_BASE));
        return *ptr;
    }
    else if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE - 3) {
        uint32_t offset = address - ROM_BASE;
        if (offset >= mem->rom_size) {
            printf("Read beyond loaded ROM size at 0x%08X (ROM size: %zu)\n", address, mem->rom_size);
            return 0;
        }
        uint32_t* ptr = (uint32_t*)(mem->rom + offset);
        return *ptr;
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE - 3) {
        if (floppy_data) {
            if (address == 0x03200000) { 
                if (mem->floppy_offset + 4 <= floppy_size) {
                    uint32_t data = *(uint32_t*)(floppy_data + mem->floppy_offset);
                    mem->floppy_offset += 4;
                    if (mem->floppy_offset >= floppy_size) mem->floppy_offset = 0; 
                    printf("Floppy read word at 0x%08X: 0x%08X\n", address, data);
                    return data;
                } else {
                    printf("Floppy read beyond size at 0x%08X\n", address);
                    return 0;
                }
            }
            else if (address == 0x03200004) { 
                return (mem->floppy_offset < floppy_size) ? 0x01 : 0x00; 
            }
        }
        printf("I/O read at 0x%08X (unimplemented)\n", address);
        return 0;
    }
    else {
        printf("Invalid read at 0x%08X\n", address);
        return 0;
    }
}

void memory_write_word(memory_t* mem, uint32_t address, uint32_t value) {
    address &= ADDR_MASK;
    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t* ptr = (uint32_t*)(mem->ram + (address - RAM_BASE));
        *ptr = value;
    }
    else if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE) {
        printf("Attempted write to ROM at 0x%08X ignored\n", address);
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        if (floppy_data) {
            if (address == 0x03200008) { 
                if (value & 0x01) { 
                    mem->floppy_offset = 0;
                    printf("Floppy reset offset at 0x%08X\n", address);
                }
            }
        }
        printf("I/O write at 0x%08X = 0x%08X (unimplemented)\n", address, value);
    }
    else {
        printf("Invalid write at 0x%08X = 0x%08X\n", address, value);
    }
}

uint8_t memory_read_byte(memory_t* mem, uint32_t address) {
    address &= ADDR_MASK;
    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        return mem->ram[address - RAM_BASE];
    }
    else if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE) {
        uint32_t offset = address - ROM_BASE;
        if (offset >= mem->rom_size) {
            printf("Byte read beyond loaded ROM size at 0x%08X (ROM size: %zu)\n", address, mem->rom_size);
            return 0;
        }
        return mem->rom[offset];
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        if (floppy_data) {
            if (address == 0x03200000) { 
                if (mem->floppy_offset < floppy_size) {
                    uint8_t data = floppy_data[mem->floppy_offset++];
                    if (mem->floppy_offset >= floppy_size) mem->floppy_offset = 0;
                    printf("Floppy read byte at 0x%08X: 0x%02X\n", address, data);
                    return data;
                } else {
                    printf("Floppy read beyond size at 0x%08X\n", address);
                    return 0;
                }
            }
            else if (address == 0x03200004) { 
                return (mem->floppy_offset < floppy_size) ? 0x01 : 0x00;
            }
        }
        printf("I/O byte read at 0x%08X (unimplemented)\n", address);
        return 0;
    }
    else {
        printf("Invalid byte read at 0x%08X\n", address);
        return 0;
    }
}

void memory_write_byte(memory_t* mem, uint32_t address, uint8_t value) {
    address &= ADDR_MASK;
    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        mem->ram[address - RAM_BASE] = value;
    }
    else if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE) {
        printf("Attempted byte write to ROM at 0x%08X ignored\n", address);
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        if (floppy_data) {
            if (address == 0x03200008) { 
                if (value & 0x01) {
                    mem->floppy_offset = 0;
                    printf("Floppy reset offset at 0x%08X\n", address);
                }
            }
        }
        printf("I/O byte write at 0x%08X = 0x%02X (unimplemented)\n", address, value);
    }
    else {
        printf("Invalid byte write at 0x%08X = 0x%02X\n", address, value);
    }
}