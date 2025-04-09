#ifndef CPU_H
#define CPU_H

#include "memory.h"
#include <cstdint>

typedef struct arm3_cpu {
    uint32_t registers[16]; // R0-R15 (R15 includes PC)
    uint32_t psr;           // Program Status Register (N, Z, C, V, I, F, and mode bits)
    memory_t* memory;
} arm3_cpu_t;

arm3_cpu_t* cpu_create(memory_t* mem);
void cpu_destroy(arm3_cpu_t* cpu);
void cpu_step(arm3_cpu_t* cpu);
void cpu_reset(arm3_cpu_t* cpu);
uint32_t cpu_get_register(arm3_cpu_t* cpu, int reg); // For debugging

#endif