#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include "memory.h"

// CPSR/SPSR flag bits (ARMv3, 26-bit address mode compatible)
#define PSR_N (1 << 31)  // Negative flag
#define PSR_Z (1 << 30)  // Zero flag
#define PSR_C (1 << 29)  // Carry flag
#define PSR_V (1 << 28)  // Overflow flag
#define PSR_I (1 << 7)   // IRQ disable bit
#define PSR_F (1 << 6)   // FIQ disable bit
#define PSR_T (1 << 5)   // Thumb state (not used in ARMv3, here for completeness)
#define PSR_MODE_MASK 0x1F // Mode bits mask (lower 5 bits)

// Processor modes (ARMv3)
#define MODE_USR 0x10    // User mode
#define MODE_FIQ 0x11    // Fast Interrupt mode
#define MODE_IRQ 0x12    // Interrupt mode
#define MODE_SVC 0x13    // Supervisor mode
#define MODE_ABT 0x17    // Abort mode (not fully implemented yet)
#define MODE_UND 0x1B    // Undefined mode (not fully implemented yet)
#define MODE_SYS 0x1F    // System mode (not used in ARMv3, here for completeness)

// Exception vectors (26-bit address space)
#define VECTOR_RESET 0x00000000
#define VECTOR_UNDEF 0x00000004
#define VECTOR_SWI   0x00000008
#define VECTOR_PABT  0x0000000C
#define VECTOR_DABT  0x00000010
#define VECTOR_IRQ   0x00000018
#define VECTOR_FIQ   0x0000001C

typedef struct arm3_cpu {
    memory_t* mem;         // Pointer to memory subsystem (includes io_t)
    uint32_t registers[16]; // General-purpose registers (R0-R15, where R15 is PC)
    uint32_t cpsr;         // Current Program Status Register
    uint32_t spsr;         // Saved Program Status Register (general, e.g., SVC mode)
    uint32_t spsr_irq;     // Saved PSR for IRQ mode
    uint32_t spsr_fiq;     // Saved PSR for FIQ mode
} arm3_cpu_t;

// Function declarations
arm3_cpu_t* cpu_create(memory_t* mem);
void cpu_destroy(arm3_cpu_t* cpu);
void cpu_reset(arm3_cpu_t* cpu);
void cpu_step(arm3_cpu_t* cpu);

#endif