#include "cpu.h"
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>

// Condition codes
enum Condition {
    EQ = 0, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV
};

// Operating modes
enum Mode {
    USER = 0x10, FIQ = 0x11, IRQ = 0x12, SVC = 0x13
};

// PSR bit masks (26-bit mode, ARM3-compatible)
#define N_FLAG (1 << 31)
#define Z_FLAG (1 << 30)
#define C_FLAG (1 << 29)
#define V_FLAG (1 << 28)
#define I_FLAG (1 << 27) // IRQ disable
#define F_FLAG (1 << 26) // FIQ disable
#define MODE_MASK 0x1F

arm3_cpu_t* cpu_create(memory_t* mem) {
    arm3_cpu_t* cpu = (arm3_cpu_t*)malloc(sizeof(arm3_cpu_t));
    if (!cpu) return nullptr;
    cpu->memory = mem;
    cpu_reset(cpu);
    return cpu;
}

void cpu_destroy(arm3_cpu_t* cpu) {
    if (cpu) free(cpu);
}

void cpu_reset(arm3_cpu_t* cpu) {
    for (int i = 0; i < 15; i++) cpu->registers[i] = 0;
    cpu->registers[15] = 0x03400000; // Start at ROM base (ARM3 reset vector)
    cpu->psr = I_FLAG | (SVC & MODE_MASK); // IRQs disabled, SVC mode
}

uint32_t cpu_get_register(arm3_cpu_t* cpu, int reg) {
    return cpu->registers[reg];
}

static bool check_condition(arm3_cpu_t* cpu, uint32_t cond) {
    bool n = cpu->psr & N_FLAG;
    bool z = cpu->psr & Z_FLAG;
    bool c = cpu->psr & C_FLAG;
    bool v = cpu->psr & V_FLAG;
    switch (cond) {
        case EQ: return z;
        case NE: return !z;
        case CS: return c;
        case CC: return !c;
        case MI: return n;
        case PL: return !n;
        case VS: return v;
        case VC: return !v;
        case HI: return c && !z;
        case LS: return !c || z;
        case GE: return n == v;
        case LT: return n != v;
        case GT: return !z && (n == v);
        case LE: return z || (n != v);
        case AL: return true;
        case NV: return false;
        default: return false;
    }
}

static void update_flags(arm3_cpu_t* cpu, uint32_t result, bool carry, bool overflow) {
    cpu->psr = (cpu->psr & ~(N_FLAG | Z_FLAG | C_FLAG | V_FLAG)) |
               (result & 0x80000000 ? N_FLAG : 0) |
               (result == 0 ? Z_FLAG : 0) |
               (carry ? C_FLAG : 0) |
               (overflow ? V_FLAG : 0);
}

static uint32_t get_operand2(arm3_cpu_t* cpu, uint32_t instr, bool* carry_out) {
    bool i_bit = (instr >> 25) & 1;
    uint32_t operand2 = instr & 0xFFF;
    uint32_t rm = instr & 0xF;
    if (i_bit) { // Immediate
        uint32_t imm = operand2 & 0xFF;
        uint32_t rotate = (operand2 >> 8) & 0xF;
        uint32_t result = (imm >> (2 * rotate)) | (imm << (32 - 2 * rotate));
        *carry_out = cpu->psr & C_FLAG; // Carry unchanged for immediate
        return result;
    } else { // Register
        bool shift_by_reg = (instr >> 4) & 1;
        uint32_t shift = (instr >> 7) & 0x1F;
        uint32_t shift_type = (instr >> 5) & 3;
        uint32_t value = cpu->registers[rm];
        if (shift_by_reg) {
            uint32_t rs = (instr >> 8) & 0xF;
            shift = cpu->registers[rs] & 0xFF;
        }
        switch (shift_type) {
            case 0: // LSL
                if (shift == 0) return value;
                *carry_out = (value >> (32 - shift)) & 1;
                return value << shift;
            case 1: // LSR
                if (shift == 0) shift = 32;
                *carry_out = (value >> (shift - 1)) & 1;
                return value >> shift;
            case 2: // ASR
                if (shift == 0) shift = 32;
                *carry_out = (value >> (shift - 1)) & 1;
                return (int32_t)value >> shift;
            case 3: // ROR
                if (shift == 0) { // RRX
                    *carry_out = value & 1;
                    return (value >> 1) | ((cpu->psr & C_FLAG) ? 0x80000000 : 0);
                }
                *carry_out = (value >> (shift - 1)) & 1;
                return (value >> shift) | (value << (32 - shift));
        }
    }
    return 0;
}

static void switch_mode(arm3_cpu_t* cpu, uint32_t new_mode) {
    cpu->psr = (cpu->psr & ~MODE_MASK) | (new_mode & MODE_MASK);
}

void cpu_step(arm3_cpu_t* cpu) {
    uint32_t pc = cpu->registers[15] & 0x03FFFFFC;
    if (pc < RAM_BASE || (pc >= RAM_BASE + RAM_SIZE && pc < IO_BASE) ||
        (pc >= IO_BASE + IO_SIZE && pc < ROM_BASE) || pc >= ROM_BASE + ROM_SIZE) {
        printf("PC out of bounds: 0x%08X, halting\n", pc);
        cpu->registers[15] = pc; // Freeze PC for debugging
        return;
    }

    uint32_t instr = memory_read_word(cpu->memory, pc);
    cpu->registers[15] = (cpu->registers[15] & ~0x03FFFFFC) | ((pc + 4) & 0x03FFFFFC); // Increment PC

    uint32_t cond = (instr >> 28) & 0xF;
    if (!check_condition(cpu, cond)) return;

    uint32_t opcode = (instr >> 21) & 0xF;
    uint32_t i_bit = (instr >> 25) & 1;
    uint32_t s_bit = (instr >> 20) & 1;
    uint32_t rn = (instr >> 16) & 0xF;
    uint32_t rd = (instr >> 12) & 0xF;
    uint32_t rm = instr & 0xF;

    if ((instr & 0x0C000000) == 0x00000000) { // Data Processing
        bool carry = cpu->psr & C_FLAG;
        bool overflow = false;
        uint32_t op1 = cpu->registers[rn];
        uint32_t op2 = get_operand2(cpu, instr, &carry);
        uint32_t result = 0;
        bool write_result = true;

        switch (opcode) {
            case 0x0: result = op1 & op2; break; // AND
            case 0x1: result = op1 ^ op2; break; // EOR
            case 0x2: // SUB
                result = op1 - op2;
                carry = op1 >= op2;
                overflow = ((op1 ^ result) & (op1 ^ op2)) >> 31;
                break;
            case 0x3: // RSB
                result = op2 - op1;
                carry = op2 >= op1;
                overflow = ((op2 ^ result) & (op2 ^ op1)) >> 31;
                break;
            case 0x4: // ADD
                result = op1 + op2;
                carry = result < op1;
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                break;
            case 0x5: // ADC
                result = op1 + op2 + (cpu->psr & C_FLAG ? 1 : 0);
                carry = result < op1 || (result == op1 && (cpu->psr & C_FLAG));
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                break;
            case 0x6: // SBC
                result = op1 - op2 - (cpu->psr & C_FLAG ? 0 : 1);
                carry = op1 >= op2 && (op1 > op2 || (cpu->psr & C_FLAG));
                overflow = ((op1 ^ result) & (op1 ^ op2)) >> 31;
                break;
            case 0x7: // RSC
                result = op2 - op1 - (cpu->psr & C_FLAG ? 0 : 1);
                carry = op2 >= op1 && (op2 > op1 || (cpu->psr & C_FLAG));
                overflow = ((op2 ^ result) & (op2 ^ op1)) >> 31;
                break;
            case 0x8: result = op1 & op2; write_result = false; break; // TST
            case 0x9: result = op1 ^ op2; write_result = false; break; // TEQ
            case 0xA: // CMP
                result = op1 - op2;
                carry = op1 >= op2;
                overflow = ((op1 ^ result) & (op1 ^ op2)) >> 31;
                write_result = false;
                break;
            case 0xB: // CMN
                result = op1 + op2;
                carry = result < op1;
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                write_result = false;
                break;
            case 0xC: result = op1 | op2; break; // ORR
            case 0xD: result = op2; break; // MOV
            case 0xE: result = op1 & ~op2; break; // BIC
            case 0xF: result = ~op2; break; // MVN
        }

        if (write_result && rd != 15) {
            cpu->registers[rd] = result;
        }
        if (s_bit || !write_result) {
            update_flags(cpu, result, carry, overflow);
        }
        if (write_result && rd == 15) {
            cpu->registers[15] = (result & 0x03FFFFFF) | (cpu->psr & 0xFC000000);
            printf("PC written: 0x%08X from instr 0x%08X at 0x%08X\n", cpu->registers[15], instr, pc);
        }
    }
    else if ((instr & 0x0F8000F0) == 0x00000090) { // Multiply (MUL, MLA) - ARM3 hardware multiplier
        uint32_t rs = (instr >> 8) & 0xF;
        uint32_t rm = instr & 0xF;
        uint32_t ra = (instr >> 16) & 0xF;
        bool accumulate = (instr >> 21) & 1;
        uint32_t result = cpu->registers[rm] * cpu->registers[rs];
        if (accumulate) result += cpu->registers[ra];
        cpu->registers[rd] = result;
        if (s_bit) {
            cpu->psr = (cpu->psr & ~(N_FLAG | Z_FLAG)) |
                       (result & 0x80000000 ? N_FLAG : 0) |
                       (result == 0 ? Z_FLAG : 0);
        }
    }
    else if ((instr & 0x0C000000) == 0x04000000) { // Single Data Transfer
        bool load = (instr >> 20) & 1;
        bool byte = (instr >> 22) & 1;
        bool writeback = (instr >> 21) & 1;
        bool pre = (instr >> 24) & 1;
        bool up = (instr >> 23) & 1;
        uint32_t offset = i_bit ? (instr & 0xFFF) : cpu->registers[rm];
        uint32_t addr = cpu->registers[rn];
        if (pre) {
            addr = up ? addr + offset : addr - offset;
        }
        if (load) {
            if (byte) {
                cpu->registers[rd] = memory_read_byte(cpu->memory, addr);
            } else {
                cpu->registers[rd] = memory_read_word(cpu->memory, addr);
            }
        } else {
            if (byte) {
                memory_write_byte(cpu->memory, addr, cpu->registers[rd] & 0xFF);
            } else {
                memory_write_word(cpu->memory, addr, cpu->registers[rd]);
            }
        }
        if (!pre) {
            addr = up ? addr + offset : addr - offset;
        }
        if (writeback || !pre) {
            cpu->registers[rn] = addr;
        }
        if (rd == 15 && load) {
            cpu->registers[15] = (cpu->registers[15] & 0x03FFFFFF) | (cpu->psr & 0xFC000000);
            printf("PC loaded: 0x%08X from addr 0x%08X, instr 0x%08X at 0x%08X\n", cpu->registers[15], addr, instr, pc);
        }
    }
    else if ((instr & 0x0E000000) == 0x08000000) { // Branch
        bool link = (instr >> 24) & 1;
        int32_t offset = (instr & 0x00FFFFFF) << 2;
        if (offset & 0x02000000) offset |= 0xFC000000; // Sign-extend
        if (link) {
            cpu->registers[14] = cpu->registers[15] - 4;
        }
        uint32_t new_pc = (cpu->registers[15] + offset) & 0x03FFFFFF;
        cpu->registers[15] = (cpu->registers[15] & 0xFC000000) | new_pc;
        printf("Branch to: 0x%08X from instr 0x%08X at 0x%08X\n", new_pc, instr, pc);
    }
    else if ((instr & 0x0E000000) == 0x0A000000) { // Block Data Transfer
        bool load = (instr >> 20) & 1;
        bool pre = (instr >> 24) & 1;
        bool up = (instr >> 23) & 1;
        bool psr = (instr >> 22) & 1;
        bool writeback = (instr >> 21) & 1;
        uint32_t reg_list = instr & 0xFFFF;
        uint32_t addr = cpu->registers[rn];
        uint32_t old_psr = cpu->psr;

        if (psr && load) {
            switch_mode(cpu, USER);
        }

        int count = 0;
        for (int i = 0; i < 16; i++) {
            if (reg_list & (1 << i)) count++;
        }
        uint32_t start_addr = addr;
        if (up) {
            if (pre) addr += 4;
        } else {
            addr -= count * 4;
            if (pre) addr += 4;
        }

        for (int i = 0; i < 16; i++) {
            if (reg_list & (1 << i)) {
                if (load) {
                    cpu->registers[i] = memory_read_word(cpu->memory, addr);
                } else {
                    memory_write_word(cpu->memory, addr, cpu->registers[i]);
                }
                addr += 4;
            }
        }

        if (writeback) {
            cpu->registers[rn] = up ? start_addr + count * 4 : start_addr - count * 4;
        }
        if (load && (reg_list & (1 << 15))) {
            cpu->registers[15] = (cpu->registers[15] & 0x03FFFFFF) | (cpu->psr & 0xFC000000);
            if (psr) {
                cpu->psr = memory_read_word(cpu->memory, addr - 4);
            }
            printf("PC loaded from block: 0x%08X, instr 0x%08X at 0x%08X\n", cpu->registers[15], instr, pc);
        }
        if (psr && load) {
            switch_mode(cpu, old_psr & MODE_MASK);
        }
    }
    else if ((instr & 0x0F000000) == 0x0F000000) { // Software Interrupt
        cpu->registers[14] = cpu->registers[15] - 4;
        cpu->psr |= I_FLAG;
        switch_mode(cpu, SVC);
        cpu->registers[15] = 0x00000008 | (cpu->psr & 0xFC000000);
        printf("SWI: PC set to 0x%08X, instr 0x%08X at 0x%08X\n", cpu->registers[15], instr, pc);
    }
    else if ((instr & 0x0E000000) == 0x0C000000) { // Coprocessor Data Operations
        printf("Coprocessor instruction 0x%08X at PC=0x%08X (unimplemented)\n", instr, pc);
    }
    else {
        printf("Unsupported instruction: 0x%08X at PC=0x%08X\n", instr, pc);
    }
}