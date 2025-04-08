#include "cpu.h"
#include <cstdint>
#include <stdlib.h>
#include <stdio.h>

// Condition codes
enum Condition {
    EQ = 0, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV
};

// Operating modes (simplified for ARM2)
enum Mode {
    USER = 0x10, FIQ = 0x11, IRQ = 0x12, SVC = 0x13
};

arm2_cpu_t* cpu_create(memory_t* mem) {
    arm2_cpu_t* cpu = (arm2_cpu_t*)malloc(sizeof(arm2_cpu_t));
    cpu->memory = mem;
    cpu_reset(cpu);
    return cpu;
}

void cpu_destroy(arm2_cpu_t* cpu) {
    free(cpu);
}

static bool check_condition(arm2_cpu_t* cpu, uint32_t cond) {
    switch (cond) {
        case EQ: return cpu->z_flag;
        case NE: return !cpu->z_flag;
        case CS: return cpu->c_flag;
        case CC: return !cpu->c_flag;
        case MI: return cpu->n_flag;
        case PL: return !cpu->n_flag;
        case VS: return cpu->v_flag;
        case VC: return !cpu->v_flag;
        case HI: return cpu->c_flag && !cpu->z_flag;
        case LS: return !cpu->c_flag || cpu->z_flag;
        case GE: return cpu->n_flag == cpu->v_flag;
        case LT: return cpu->n_flag != cpu->v_flag;
        case GT: return !cpu->z_flag && (cpu->n_flag == cpu->v_flag);
        case LE: return cpu->z_flag || (cpu->n_flag != cpu->v_flag);
        case AL: return true;
        case NV: return false; // Never (reserved)
        default: return false;
    }
}

static void update_flags(arm2_cpu_t* cpu, uint32_t result, bool carry, bool overflow) {
    cpu->n_flag = (result & 0x80000000) != 0;
    cpu->z_flag = (result == 0);
    cpu->c_flag = carry;
    cpu->v_flag = overflow;
}

void cpu_step(arm2_cpu_t* cpu) {
    uint32_t pc = cpu->registers[15] & 0x03FFFFFC; // Align to word, mask PSR
    uint32_t instr = memory_read_word(cpu->memory, pc);
    cpu->registers[15] += 4; // Increment PC

    uint32_t cond = (instr >> 28) & 0xF;
    if (!check_condition(cpu, cond)) return;

    uint32_t opcode = (instr >> 21) & 0xF;
    uint32_t i_bit = (instr >> 25) & 1; // Immediate bit
    uint32_t s_bit = (instr >> 20) & 1; // Set condition codes
    uint32_t rn = (instr >> 16) & 0xF;  // Base register
    uint32_t rd = (instr >> 12) & 0xF;  // Destination register
    uint32_t operand2 = instr & 0xFFF;  // Lower 12 bits for operand2
    uint32_t rm = instr & 0xF;          // Register for operand2
    bool carry = cpu->c_flag;
    bool overflow = false;

    if ((instr & 0x0FC00000) == 0x00000000) { // Data Processing
        uint32_t op1 = cpu->registers[rn];
        uint32_t op2 = i_bit ? (operand2 >> (2 * ((instr >> 8) & 0xF))) | (operand2 << (32 - 2 * ((instr >> 8) & 0xF)))
                             : cpu->registers[rm];
        uint32_t result;

        switch (opcode) {
            case 0x0: // AND
                result = op1 & op2;
                break;
            case 0x1: // EOR
                result = op1 ^ op2;
                break;
            case 0x2: // SUB
                result = op1 - op2;
                carry = (op1 >= op2);
                overflow = ((op1 ^ result) & (op1 ^ op2)) >> 31;
                break;
            case 0x4: // ADD
                result = op1 + op2;
                carry = (result < op1);
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                break;
            case 0xC: // ORR
                result = op1 | op2;
                break;
            case 0xD: // MOV
                result = op2;
                break;
            default:
                printf("Unsupported opcode: 0x%X\n", opcode);
                return;
        }

        if (rd != 15) { // Don’t write to PC yet
            cpu->registers[rd] = result;
        }
        if (s_bit) {
            update_flags(cpu, result, carry, overflow);
        }
        if (rd == 15) { // PC write
            cpu->registers[15] = (result & 0x03FFFFFF) | (cpu->registers[15] & 0xFC000000); // Preserve PSR bits
        }
    }
    else if ((instr & 0x0E000000) == 0x08000000) { // Branch
        int32_t offset = ((instr & 0x00FFFFFF) << 2) | ((instr & 0x00800000) ? 0xFC000000 : 0); // Sign-extend
        bool link = (instr >> 24) & 1;
        if (link) {
            cpu->registers[14] = cpu->registers[15] - 4; // Save return address
        }
        cpu->registers[15] += offset;
    }
    else if ((instr & 0x0E000000) == 0x04000000) { // Single Data Transfer
        bool load = (instr >> 20) & 1;
        bool pre = (instr >> 24) & 1;
        bool up = (instr >> 23) & 1;
        uint32_t offset = i_bit ? operand2 : cpu->registers[rm];
        uint32_t addr = cpu->registers[rn];
        if (pre) {
            addr = up ? addr + offset : addr - offset;
        }
        if (load) {
            cpu->registers[rd] = memory_read_word(cpu->memory, addr);
        } else {
            memory_write_word(cpu->memory, addr, cpu->registers[rd]);
        }
        if (!pre) {
            cpu->registers[rn] = up ? addr + offset : addr - offset;
        }
    }
    else {
        printf("Unsupported instruction: 0x%08X at PC=0x%08X\n", instr, pc);
    }
}

void cpu_reset(arm2_cpu_t* cpu) {
    for (int i = 0; i < 15; i++) cpu->registers[i] = 0;
    cpu->registers[15] = 0x03400000; // Start at ROM base (typical for Archimedes)
    cpu->n_flag = false;
    cpu->z_flag = false;
    cpu->c_flag = false;
    cpu->v_flag = false;
    cpu->mode = SVC; // Supervisor mode
}

uint32_t cpu_get_register(arm2_cpu_t* cpu, int reg) {
    return cpu->registers[reg];
}