#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include "memory.h"

#define PSR_N (1 << 31)
#define PSR_Z (1 << 30)
#define PSR_C (1 << 29)
#define PSR_V (1 << 28)
#define PSR_I (1 << 7)
#define PSR_F (1 << 6)
#define PSR_MODE_MASK 0x1F
#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13

typedef struct {
    uint32_t registers[16]; // R0-R15 (R15 is PC)
    uint32_t cpsr; // Current Program Status Register
    uint32_t spsr; // Saved Program Status Register
    memory_t* memory;
} arm3_cpu_t;

arm3_cpu_t* cpu_create(memory_t* mem);
void cpu_destroy(arm3_cpu_t* cpu);
void cpu_reset(arm3_cpu_t* cpu);
void cpu_step(arm3_cpu_t* cpu);

#endif