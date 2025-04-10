#include "memory.h"
#include "io.h" // Include io.h to get the full definition of io_t
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

memory_t* memory_create(const char* jfd_path, uint32_t rom_base, io_t* io) {    
	memory_t* mem = (memory_t*)malloc(sizeof(memory_t));
    if (!mem) {
        printf("Failed to allocate memory struct\n");
        return NULL;
    }
	
    mem->ram = (uint8_t*)calloc(RAM_SIZE, 1); 
    mem->rom = (uint8_t*)calloc(ROM_SIZE, 1); 
    mem->rom_size = 0;
    mem->rom_base = rom_base ? rom_base : ROM_DEFAULT_BASE;
    mem->floppy_offset = 0;
    mem->io = io;
	mem->is_boot_mode = 1;

    if (!mem->ram || !mem->rom) {
        printf("Failed to allocate RAM or ROM\n");
        free(mem->ram);
        free(mem->rom);
        free(mem);
        return NULL;
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
                printf("Loaded ROM: %zu bytes into ROM at 0x%08X\n", mem->rom_size, mem->rom_base);
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
    static uint32_t last_logged_address = 0xFFFFFFFF;
    static int log_counter = 0;

    address &= ADDR_MASK;
    // Alias ROM at 0x02000000 and 0x00000000 to match Acorn Archimedes boot behavior
    if ((address >= 0x02000000 && address < 0x02200000) || 
        (address >= 0x00000000 && address < 0x00200000)) {
        uint32_t rom_offset = (address & 0x001FFFFF) % mem->rom_size; // Wrap around ROM size
        if (rom_offset <= mem->rom_size - 4) {
            uint32_t* ptr = (uint32_t*)(mem->rom + rom_offset);
            // Suppress logging for loop addresses (0x0380A588 to 0x0380A594)
            if (address < 0x0380A588 || address > 0x0380A594) {
                if (address != last_logged_address || log_counter % 1000 == 0) {
                    printf("ROM alias read at 0x%08X (offset 0x%08X): 0x%08X\n", address, rom_offset, *ptr);
                    last_logged_address = address;
                }
            }
            log_counter++;
            return *ptr;
        } else {
            printf("ROM alias read beyond size at 0x%08X (ROM size: 0x%08lX)\n", address, mem->rom_size);
            return 0xFFFFFFFF; // Indicate invalid read
        }
    }
    else if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t* ptr = (uint32_t*)(mem->ram + (address - RAM_BASE));
        return *ptr;
    }
    else if (address >= mem->rom_base && address < mem->rom_base + mem->rom_size - 3) {
        uint32_t offset = address - mem->rom_base;
        uint32_t* ptr = (uint32_t*)(mem->rom + offset);
        // Suppress logging for loop addresses (0x0380A588 to 0x0380A594)
        if (address < 0x0380A588 || address > 0x0380A594) {
            if (address != last_logged_address || log_counter % 1000 == 0) {
                printf("ROM read at 0x%08X (offset 0x%08X): 0x%08X\n", address, offset, *ptr);
                last_logged_address = address;
            }
        }
        log_counter++;
        return *ptr;
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE - 3) {
        uint32_t value = io_read_word(mem->io, mem, address);
        printf("IO read at 0x%08X: 0x%08X\n", address, value);
        return value;
    }
    else {
        printf("Invalid read at 0x%08X (RAM base: 0x%08X, RAM size: 0x%08X, ROM base: 0x%08X, ROM size: 0x%08lX, IO base: 0x%08X, IO size: 0x%08X, boot mode: %d)\n",
               address, RAM_BASE, RAM_SIZE, mem->rom_base, mem->rom_size, IO_BASE, IO_SIZE, mem->is_boot_mode);
        return 0xFFFFFFFF; // Indicate invalid read
    }
}

void memory_write_word(memory_t* mem, uint32_t address, uint32_t value) {
    if ((address >= mem->rom_base && address < mem->rom_base + mem->rom_size) ||
        (mem->is_boot_mode && (address >= 0x00000000 && address < 0x00200000))) {
        printf("Attempted write to ROM at 0x%08X ignored (boot mode: %d)\n", address, mem->is_boot_mode);
        return;
    }
    else if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t* ptr = (uint32_t*)(mem->ram + (address - RAM_BASE));
        *ptr = value;
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        io_write_word(mem->io, mem, address, value);
    }
    else {
        printf("Invalid write at 0x%08X = 0x%08X\n", address, value);
    }
}

uint8_t memory_read_byte(memory_t* mem, uint32_t address) {
    address &= ADDR_MASK;
    if ((address >= 0x02000000 && address < 0x02200000) || 
        (address >= 0x00000000 && address < 0x00200000)) {
        uint32_t rom_offset = (address & 0x001FFFFF) % mem->rom_size;
        if (rom_offset < mem->rom_size) {
            return mem->rom[rom_offset];
        } else {
            printf("ROM alias byte read beyond size at 0x%08X\n", address);
            return 0;
        }
    }
    else if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        return mem->ram[address - RAM_BASE];
    }
    else if (address >= mem->rom_base && address < mem->rom_base + mem->rom_size) {
        uint32_t offset = address - mem->rom_base;
        return mem->rom[offset];
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        return io_read_byte(mem->io, mem, address);
    }
    else {
        printf("Invalid byte read at 0x%08X\n", address);
        return 0;
    }
}

void memory_write_byte(memory_t* mem, uint32_t address, uint8_t value) {
    address &= ADDR_MASK;
    if ((address >= 0x02000000 && address < 0x02200000) || 
        (address >= 0x00000000 && address < 0x00200000)) {
        printf("Attempted byte write to ROM alias at 0x%08X ignored\n", address);
        return;
    }
    else if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        mem->ram[address - RAM_BASE] = value;
    }
    else if (address >= mem->rom_base && address < mem->rom_base + mem->rom_size) {
        printf("Attempted byte write to ROM at 0x%08X ignored\n", address);
    }
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
        io_write_byte(mem->io, mem, address, value);
    }
    else {
        printf("Invalid byte write at 0x%08X = 0x%02X\n", address, value);
    }
}