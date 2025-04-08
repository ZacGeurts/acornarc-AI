#ifndef CPU_H
#define CPU_H

#include "memory.h"
#include <cstdint>

typedef struct arm2_cpu {
    uint32_t registers[16]; // R0-R15 (R15 is PC + PSR bits in 26-bit mode)
    memory_t* memory;
    bool n_flag; // Negative
    bool z_flag; // Zero
    bool c_flag; // Carry
    bool v_flag; // Overflow
    uint32_t mode; // Current mode (simplified)
} arm2_cpu_t;

arm2_cpu_t* cpu_create(memory_t* mem);
void cpu_destroy(arm2_cpu_t* cpu);
void cpu_step(arm2_cpu_t* cpu);
void cpu_reset(arm2_cpu_t* cpu);
uint32_t cpu_get_register(arm2_cpu_t* cpu, int reg); // For debugging

#endif