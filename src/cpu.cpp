#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>

#define ROM_BASE 0x03800000 // Updated to match branch target
#define ADDR_MASK 0x03FFFFFF // 26-bit address space for ARMv3 (Acorn Archimedes)

arm3_cpu_t* cpu_create(memory_t* mem) {
    arm3_cpu_t* cpu = (arm3_cpu_t*)malloc(sizeof(arm3_cpu_t));
    if (!cpu) {
        printf("Failed to allocate CPU\n");
        return NULL; // Changed from nullptr to NULL for C compatibility
    }
    
    cpu->memory = mem;
    for (int i = 0; i < 16; i++) {
        cpu->registers[i] = 0;
    }
    cpu->cpsr = PSR_I | PSR_F | MODE_SVC; // Supervisor mode, interrupts off
    cpu->spsr = 0;
    cpu_reset(cpu);
    
    return cpu;
}

void cpu_destroy(arm3_cpu_t* cpu) {
    if (cpu) {
        free(cpu);
    }
}

void cpu_reset(arm3_cpu_t* cpu) {
    for (int i = 0; i < 16; i++) {
        cpu->registers[i] = 0;
    }
    cpu->registers[15] = 0x00000000; // Start at reset vector
    cpu->cpsr = PSR_I | PSR_F | MODE_SVC;
    cpu->spsr = 0;
    printf("CPU reset: PC = 0x%08X\n", cpu->registers[15]);
}

static void update_flags(arm3_cpu_t* cpu, uint32_t result, uint32_t op1, uint32_t op2, int carry, int overflow) {
    cpu->cpsr &= ~(PSR_N | PSR_Z | PSR_C | PSR_V);
    if (result & 0x80000000) cpu->cpsr |= PSR_N;
    if (result == 0) cpu->cpsr |= PSR_Z;
    if (carry) cpu->cpsr |= PSR_C;
    if (overflow) cpu->cpsr |= PSR_V;
}

static int condition_met(arm3_cpu_t* cpu, uint32_t cond) {
    switch (cond) {
        case 0x0: return (cpu->cpsr & PSR_Z) != 0;                      // EQ
        case 0x1: return (cpu->cpsr & PSR_Z) == 0;                      // NE
        case 0x2: return (cpu->cpsr & PSR_C) != 0;                      // CS/HS
        case 0x3: return (cpu->cpsr & PSR_C) == 0;                      // CC/LO
        case 0x4: return (cpu->cpsr & PSR_N) != 0;                      // MI
        case 0x5: return (cpu->cpsr & PSR_N) == 0;                      // PL
        case 0x6: return (cpu->cpsr & PSR_V) != 0;                      // VS
        case 0x7: return (cpu->cpsr & PSR_V) == 0;                      // VC
        case 0x8: return (cpu->cpsr & PSR_C) != 0 && (cpu->cpsr & PSR_Z) == 0; // HI
        case 0x9: return (cpu->cpsr & PSR_C) == 0 || (cpu->cpsr & PSR_Z) != 0; // LS
        case 0xA: return ((cpu->cpsr & PSR_N) >> 31) == ((cpu->cpsr & PSR_V) >> 28); // GE
        case 0xB: return ((cpu->cpsr & PSR_N) >> 31) != ((cpu->cpsr & PSR_V) >> 28); // LT
        case 0xC: return (cpu->cpsr & PSR_Z) == 0 && ((cpu->cpsr & PSR_N) >> 31) == ((cpu->cpsr & PSR_V) >> 28); // GT
        case 0xD: return (cpu->cpsr & PSR_Z) != 0 || ((cpu->cpsr & PSR_N) >> 31) != ((cpu->cpsr & PSR_V) >> 28); // LE
        case 0xE: return 1;                                             // AL
        case 0xF: return 0;                                             // NV (reserved)
        default: return 0;
    }
}

static uint32_t get_operand2(arm3_cpu_t* cpu, uint32_t instr, int* carry_out) {
    uint32_t operand2;
    if (instr & (1 << 25)) { // Immediate
        uint32_t imm = instr & 0xFF;
        uint32_t rot = (instr >> 8) & 0xF;
        operand2 = (imm >> (2 * rot)) | (imm << (32 - 2 * rot));
        *carry_out = (rot == 0) ? (cpu->cpsr & PSR_C) : ((operand2 >> 31) & 1);
    } else { // Register
        uint32_t rm = instr & 0xF;
        operand2 = cpu->registers[rm];
        uint32_t shift = (instr >> 4) & 0xFF;
        if (shift & 0x1) { // Register shift
            uint32_t rs = (instr >> 8) & 0xF;
            uint32_t shift_amount = cpu->registers[rs] & 0xFF;
            switch ((shift >> 1) & 0x3) {
                case 0: // LSL
                    if (shift_amount > 32) { operand2 = 0; *carry_out = 0; }
                    else if (shift_amount == 32) { *carry_out = operand2 & 1; operand2 = 0; }
                    else if (shift_amount > 0) { *carry_out = (operand2 >> (32 - shift_amount)) & 1; operand2 <<= shift_amount; }
                    break;
                case 1: // LSR
                    if (shift_amount > 32) { operand2 = 0; *carry_out = 0; }
                    else if (shift_amount == 32) { *carry_out = (operand2 >> 31) & 1; operand2 = 0; }
                    else if (shift_amount > 0) { *carry_out = (operand2 >> (shift_amount - 1)) & 1; operand2 >>= shift_amount; }
                    break;
                case 2: // ASR
                    if (shift_amount >= 32) { *carry_out = (operand2 >> 31) & 1; operand2 = (int32_t)operand2 >> 31; }
                    else { *carry_out = (operand2 >> (shift_amount - 1)) & 1; operand2 = (int32_t)operand2 >> shift_amount; }
                    break;
                case 3: // ROR
                    if (shift_amount == 0) break; // RRX not implemented yet
                    shift_amount &= 31;
                    if (shift_amount > 0) { *carry_out = (operand2 >> (shift_amount - 1)) & 1; operand2 = (operand2 >> shift_amount) | (operand2 << (32 - shift_amount)); }
                    break;
            }
        } else { // Immediate shift
            uint32_t shift_amount = (shift >> 3) & 0x1F;
            switch ((shift >> 1) & 0x3) {
                case 0: // LSL
                    if (shift_amount > 0) { *carry_out = (operand2 >> (32 - shift_amount)) & 1; operand2 <<= shift_amount; }
                    break;
                case 1: // LSR
                    if (shift_amount == 0) { operand2 = 0; *carry_out = (operand2 >> 31) & 1; }
                    else { *carry_out = (operand2 >> (shift_amount - 1)) & 1; operand2 >>= shift_amount; }
                    break;
                case 2: // ASR
                    if (shift_amount == 0) { *carry_out = (operand2 >> 31) & 1; operand2 = (int32_t)operand2 >> 31; }
                    else { *carry_out = (operand2 >> (shift_amount - 1)) & 1; operand2 = (int32_t)operand2 >> shift_amount; }
                    break;
                case 3: // ROR
                    if (shift_amount == 0) break; // RRX not implemented yet
                    *carry_out = (operand2 >> (shift_amount - 1)) & 1;
                    operand2 = (operand2 >> shift_amount) | (operand2 << (32 - shift_amount));
                    break;
            }
        }
    }
    return operand2;
}

void cpu_step(arm3_cpu_t* cpu) {
    uint32_t fetch_pc = cpu->registers[15] & ADDR_MASK; // Fetch stage PC
    uint32_t instr = memory_read_word(cpu->memory, fetch_pc);
    if (instr == 0xFFFFFFFF) { // Assuming memory_read_word returns -1 on invalid read
        printf("Invalid read at 0x%08X\n", fetch_pc);
        return;
    }
    printf("PC: 0x%08X, Instr: 0x%08X\n", fetch_pc, instr);
    cpu->registers[15] += 4; // Advance PC for next fetch (pipeline: fetch, decode, execute)

    uint32_t cond = (instr >> 28) & 0xF;
    if (!condition_met(cpu, cond)) {
        return;
    }

    // Data Processing
    if ((instr & 0x0C000000) == 0x00000000) {
        uint32_t opcode = (instr >> 21) & 0xF;
        uint32_t rn = (instr >> 16) & 0xF;
        uint32_t rd = (instr >> 12) & 0xF;
        int s_flag = (instr >> 20) & 1;
        int carry_in = (cpu->cpsr & PSR_C) ? 1 : 0;
        int carry_out = carry_in;
        uint32_t op1 = cpu->registers[rn];
        uint32_t op2 = get_operand2(cpu, instr, &carry_out);
        uint32_t result;
        int overflow = 0;

        switch (opcode) {
            case 0x0: result = op1 & op2; break; // AND
            case 0x1: result = op1 ^ op2; break; // EOR
            case 0x2: // SUB
                result = op1 - op2;
                overflow = ((op1 ^ result) & (~op2 ^ result)) >> 31;
                carry_out = (op1 >= op2);
                break;
            case 0x3: // RSB
                result = op2 - op1;
                overflow = ((op2 ^ result) & (~op1 ^ result)) >> 31;
                carry_out = (op2 >= op1);
                break;
            case 0x4: // ADD
                result = op1 + op2;
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                carry_out = (result < op1);
                break;
            case 0x5: // ADC
                result = op1 + op2 + carry_in;
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                carry_out = (result < op1) || (result == op1 && op2 != 0);
                break;
            case 0x6: // SBC
                result = op1 - op2 + carry_in - 1;
                overflow = ((op1 ^ result) & (~op2 ^ result)) >> 31;
                carry_out = (op1 >= op2) || (op1 == op2 && carry_in);
                break;
            case 0x7: // RSC
                result = op2 - op1 + carry_in - 1;
                overflow = ((op2 ^ result) & (~op1 ^ result)) >> 31;
                carry_out = (op2 >= op1) || (op2 == op1 && carry_in);
                break;
            case 0x8: // TST
                result = op1 & op2;
                if (rd != 0) printf("Invalid TST with Rd != 0 at 0x%08X\n", fetch_pc);
                break;
            case 0x9: // TEQ
                result = op1 ^ op2;
                if (rd != 0) printf("Invalid TEQ with Rd != 0 at 0x%08X\n", fetch_pc);
                break;
            case 0xA: // CMP
                result = op1 - op2;
                overflow = ((op1 ^ result) & (~op2 ^ result)) >> 31;
                carry_out = (op1 >= op2);
                if (rd != 0) printf("Invalid CMP with Rd != 0 at 0x%08X\n", fetch_pc);
                break;
            case 0xB: // CMN
                result = op1 + op2;
                overflow = ((op1 ^ result) & (op2 ^ result)) >> 31;
                carry_out = (result < op1);
                if (rd != 0) printf("Invalid CMN with Rd != 0 at 0x%08X\n", fetch_pc);
                break;
            case 0xC: result = op1 | op2; break; // ORR
            case 0xD: result = op2; break; // MOV
            case 0xE: result = op1 & ~op2; break; // BIC
            case 0xF: result = ~op2; break; // MVN
        }

        if (s_flag || opcode >= 0x8) {
            update_flags(cpu, result, op1, op2, carry_out, overflow);
        }
        if (opcode < 0x8 || opcode >= 0xC) {
            if (rd != 15) cpu->registers[rd] = result;
            else cpu->registers[15] = result & ADDR_MASK;
        }
    }
    // Branch (corrected mask and pipeline handling)
    else if ((instr & 0x0F000000) == 0x0A000000) { // B or BL
        int32_t offset = instr & 0x00FFFFFF; // Extract 24-bit offset
        if (offset & 0x00800000) offset |= 0xFF000000; // Sign-extend
        offset <<= 2; // Multiply by 4 for byte addressing
        uint32_t base_pc = fetch_pc + 8; // ARMv3 pipeline: PC + 8 at execution
        uint32_t new_pc = base_pc + offset;
        if (instr & (1 << 24)) { // Link (BL)
            cpu->registers[14] = cpu->registers[15]; // Save next instruction address
        }
        cpu->registers[15] = new_pc & ADDR_MASK;
        printf("Branch to: 0x%08X from instr 0x%08X at 0x%08X (offset: 0x%08X)\n", 
               new_pc & ADDR_MASK, instr, fetch_pc, offset);
    }
    // Load/Store (Fixed post-indexed addressing)
    else if ((instr & 0x0C000000) == 0x04000000) {
        uint32_t rn = (instr >> 16) & 0xF;
        uint32_t rd = (instr >> 12) & 0xF;
        int load = (instr >> 20) & 1;
        int byte = (instr >> 22) & 1;
        int up = (instr >> 23) & 1;
        int pre = (instr >> 24) & 1;
        int writeback = (instr >> 21) & 1;
        uint32_t base = cpu->registers[rn];
        uint32_t offset;

        if (instr & (1 << 25)) {
            int carry_out;
            offset = get_operand2(cpu, instr, &carry_out);
        } else {
            offset = instr & 0xFFF;
        }

        uint32_t addr = up ? base + offset : base - offset;
        if (pre) {
            // Pre-indexed: Compute address first, then load/store
            if (load) {
                if (byte) cpu->registers[rd] = memory_read_byte(cpu->memory, addr);
                else cpu->registers[rd] = memory_read_word(cpu->memory, addr);
            } else {
                if (byte) memory_write_byte(cpu->memory, addr, cpu->registers[rd]);
                else memory_write_word(cpu->memory, addr, cpu->registers[rd]);
            }
            if (writeback) cpu->registers[rn] = addr; // Write back the computed address
        } else {
            // Post-indexed: Load/store using base, then update base
            if (load) {
                if (byte) cpu->registers[rd] = memory_read_byte(cpu->memory, base);
                else cpu->registers[rd] = memory_read_word(cpu->memory, base);
            } else {
                if (byte) memory_write_byte(cpu->memory, base, cpu->registers[rd]);
                else memory_write_word(cpu->memory, addr, cpu->registers[rd]);
            }
            cpu->registers[rn] = addr; // Update base register with new address
        }
        if (rd == 15) cpu->registers[15] &= ADDR_MASK;
    }
    // Multiple Load/Store
    else if ((instr & 0x0E000000) == 0x08000000) {
        uint32_t rn = (instr >> 16) & 0xF;
        int load = (instr >> 20) & 1;
        int up = (instr >> 23) & 1;
        int pre = (instr >> 24) & 1;
        int writeback = (instr >> 21) & 1;
        uint32_t reg_list = instr & 0xFFFF;
        uint32_t base = cpu->registers[rn];
        int count = 0;
        for (int i = 0; i < 16; i++) if (reg_list & (1 << i)) count++;

        uint32_t addr = up ? base : base - (count * 4);
        if (!up && !pre) addr += 4;
        if (up && pre) addr += 4;

        for (int i = 0; i < 16; i++) {
            if (reg_list & (1 << i)) {
                if (load) cpu->registers[i] = memory_read_word(cpu->memory, addr);
                else memory_write_word(cpu->memory, addr, cpu->registers[i]);
                addr += 4;
            }
        }
        if (writeback) cpu->registers[rn] = up ? base + (count * 4) : base - (count * 4);
        if (load && (reg_list & (1 << 15))) cpu->registers[15] &= ADDR_MASK;
    }
    // Multiply
    else if ((instr & 0x0FC000F0) == 0x00000090) {
        uint32_t rd = (instr >> 16) & 0xF;
        uint32_t rn = (instr >> 12) & 0xF;
        uint32_t rs = (instr >> 8) & 0xF;
        uint32_t rm = instr & 0xF;
        int accumulate = (instr >> 21) & 1;
        int set_flags = (instr >> 20) & 1;

        uint32_t result = cpu->registers[rm] * cpu->registers[rs];
        if (accumulate) result += cpu->registers[rn];
        cpu->registers[rd] = result;

        if (set_flags) {
            update_flags(cpu, result, 0, 0, 0, 0); // Only N and Z flags affected
        }
    }
    // Software Interrupt
    else if ((instr & 0x0F000000) == 0x0F000000) {
        cpu->spsr = cpu->cpsr;
        cpu->cpsr = (cpu->cpsr & ~PSR_MODE_MASK) | MODE_SVC | PSR_I;
        cpu->registers[14] = cpu->registers[15]; // Save next instruction address
        cpu->registers[15] = 0x00000008 & ADDR_MASK; // SWI vector
        printf("SWI at 0x%08X, comment: 0x%06X\n", fetch_pc, instr & 0xFFFFFF);
    }
    // Unimplemented
    else {
        printf("Unimplemented instruction 0x%08X at 0x%08X\n", instr, fetch_pc);
    }
}