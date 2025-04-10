#include "cpu.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>

#define ROM_BASE 0x03800000 // Updated to match branch target
#define ADDR_MASK 0x03FFFFFF // 26-bit address space for ARMv3 (Acorn Archimedes)

arm3_cpu_t* cpu_create(memory_t* mem) {
    arm3_cpu_t* cpu = (arm3_cpu_t*)malloc(sizeof(arm3_cpu_t));
    if (!cpu) {
        printf("Failed to allocate CPU\n");
        return NULL;
    }
    
    cpu->mem = mem;
    for (int i = 0; i < 16; i++) {
        cpu->registers[i] = 0;
    }
    cpu->cpsr = PSR_I | PSR_F | MODE_SVC; // Supervisor mode, interrupts off
    cpu->spsr = 0;
    cpu->spsr_irq = 0; // Initialize SPSR for IRQ mode
    cpu->spsr_fiq = 0; // Initialize SPSR for FIQ mode
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
    cpu->registers[14] = 0x00000004; // Set R14 to the next instruction after reset
    cpu->cpsr = PSR_I | PSR_F | MODE_SVC;
    cpu->spsr = 0;
    cpu->spsr_irq = 0;
    cpu->spsr_fiq = 0;
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
    static int log_counter = 0;
    static int loop1_count = 0;
    static int loop2_count = 0;
    static int loop3_count = 0;
    static int early_loop_count = 0;
    static int outer_loop_count = 0;
    static int new_loop_count = 0;
    static int total_steps = 0;

    // Check for interrupts before fetching instruction
    if (cpu->mem->io->irq_pending && !(cpu->cpsr & PSR_I)) {
        printf("IRQ triggered at PC: 0x%08X, jumping to 0x00000018, R14: 0x%08X, CPSR: 0x%08X\n",
               cpu->registers[15], cpu->registers[14], cpu->cpsr);
        cpu->spsr_irq = cpu->cpsr;
        cpu->registers[14] = cpu->registers[15]; // Save return address
        cpu->cpsr = (cpu->cpsr & ~PSR_MODE_MASK) | MODE_IRQ | PSR_I;
        cpu->registers[15] = 0x00000018 & ADDR_MASK;
        cpu->mem->io->irq_pending = false;
        return;
    }

    uint32_t fetch_pc = cpu->registers[15] & ADDR_MASK;
    uint32_t instr = memory_read_word(cpu->mem, fetch_pc);
    if (instr == 0xFFFFFFFF) {
        printf("Invalid read at 0x%08X (PC: 0x%08X, r0: 0x%08X, r1: 0x%08X, r14: 0x%08X, CPSR: 0x%08X)\n",
               fetch_pc, cpu->registers[15], cpu->registers[0], cpu->registers[1], cpu->registers[14], cpu->cpsr);
        exit(1);
        return;
    }

    // Add debug for IRQ vector execution
    if (fetch_pc == 0x00000018) {
        printf("IRQ vector at 0x00000018: 0x%08X, R14: 0x%08X\n", instr, cpu->registers[14]);
    }

    char disasm[64] = "Unknown";
    if ((instr & 0x0E000000) == 0x0A000000) { // Branch
        int32_t offset = (instr & 0x00FFFFFF) << 2;
        if (offset & 0x02000000) offset |= 0xFC000000;
        uint32_t target = (fetch_pc + 8 + offset) & ADDR_MASK;
        int link = (instr >> 24) & 1;
        const char* conds[] = {"EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT", "LE", "", "NV"};
        uint32_t cond = (instr >> 28) & 0xF;
        sprintf(disasm, "%s%s 0x%08X", link ? "BL" : "B", conds[cond], target);
    } else if ((instr & 0x0C000000) == 0x00000000) { // Data Processing
        uint32_t opcode = (instr >> 21) & 0xF;
        uint32_t rn = (instr >> 16) & 0xF;
        uint32_t rd = (instr >> 12) & 0xF;
        uint32_t rm = instr & 0xF;
        int imm = (instr >> 25) & 1;
        int s_flag = (instr >> 20) & 1;
        const char* ops[] = {"AND", "EOR", "SUB", "RSB", "ADD", "ADC", "SBC", "RSC", 
                             "TST", "TEQ", "CMP", "CMN", "ORR", "MOV", "BIC", "MVN"};
        if (opcode == 0xD && !imm)
            sprintf(disasm, "MOV%s r%d, r%d", s_flag ? "S" : "", rd, rm);
        else if (opcode == 0xD && rd == 15 && rn == 14 && rm == 0)
            sprintf(disasm, "MOV PC, r14");
        else if (opcode == 0x2 && imm)
            sprintf(disasm, "SUB%s r%d, r%d, #0x%X", s_flag ? "S" : "", rd, rn, instr & 0xFFF);
        else if (opcode == 0x4 && imm)
            sprintf(disasm, "ADD%s r%d, r%d, #0x%X", s_flag ? "S" : "", rd, rn, instr & 0xFFF);
        else if (opcode == 0x4 && !imm)
            sprintf(disasm, "ADD%s r%d, r%d, r%d", s_flag ? "S" : "", rd, rn, rm);
        else if (opcode == 0xC && !imm)
            sprintf(disasm, "ORR%s r%d, r%d, r%d", s_flag ? "S" : "", rd, rn, rm);
        else if (opcode == 0xA && !imm)
            sprintf(disasm, "CMP r%d, r%d", rn, rm);
        else if (opcode >= 0x8 && opcode <= 0xB && imm)
            sprintf(disasm, "%s r%d, #0x%X", ops[opcode], rn, instr & 0xFFF);
        else if (imm)
            sprintf(disasm, "%s%s r%d, r%d, #0x%X", ops[opcode], s_flag ? "S" : "", rd, rn, instr & 0xFFF);
        else
            sprintf(disasm, "%s%s r%d, r%d, r%d", ops[opcode], s_flag ? "S" : "", rd, rn, rm);
    } else if ((instr & 0x0C000000) == 0x04000000) { // Load/Store
        int load = (instr >> 20) & 1;
        int byte = (instr >> 22) & 1;
        uint32_t rn = (instr >> 16) & 0xF;
        uint32_t rd = (instr >> 12) & 0xF;
        uint32_t offset = instr & 0xFFF;
        sprintf(disasm, "%s%s r%d, [r%d, #0x%X]", load ? "LDR" : "STR", byte ? "B" : "", rd, rn, offset);
    } else if ((instr & 0x0F000000) == 0x08000000) { // Block Data Transfer (LDM/STM)
        int load = (instr >> 20) & 1;
        int up = (instr >> 23) & 1;
        int pre = (instr >> 24) & 1;
        uint32_t rn = (instr >> 16) & 0xF;
        uint32_t reg_list = instr & 0xFFFF;
        const char* dir = up ? "IA" : "DA"; // Increment/Decrement After
        if (pre) dir = up ? "IB" : "DB";    // Increment/Decrement Before
        if (instr == 0xE8BD0043)
            sprintf(disasm, "LDMFD sp!, {r0,r1,r6}");
        else if (instr == 0xE92D0043)
            sprintf(disasm, "STMFD sp!, {r0,r1,r6}");
        else
            sprintf(disasm, "%s%s r%d%s, {0x%04X}", load ? "LDM" : "STM", dir, rn, load && (reg_list & (1 << 15)) ? "!" : "", reg_list);
    }

    // Debug additions (unchanged)
    if (fetch_pc == 0x0380A598) {
        printf("Post-loop at 0x0380A598: 0x%08X ; %s\n", instr, disasm);
        memory_write_word(cpu->mem, 0x03600000, 0);
        printf("Forced MEMC write to exit boot mode at 0x0380A598\n");
    }
    if (fetch_pc == 0x0380A594) {
        printf("Pre-exit state: PC=0x%08X, R0=0x%08X, R1=0x%08X, R2=0x%08X, R14=0x%08X, CPSR=0x%08X\n",
               cpu->registers[15], cpu->registers[0], cpu->registers[1], cpu->registers[2], cpu->registers[14], cpu->cpsr);
    }
    if (fetch_pc == 0x0380A5EC) {
        printf("Calling 0x0380A5EC, r2: 0x%08X, from PC: 0x%08X\n", cpu->registers[2], cpu->registers[14]);
    }
    if (fetch_pc == 0x0380A23C) {
        printf("Entering Loop 1 at 0x0380A23C, r3: 0x%08X, r5: 0x%08X\n", cpu->registers[3], cpu->registers[5]);
    }
    printf("0x%08X: 0x%08X  ; %s\n", fetch_pc, instr, disasm);

    // Additional debug (unchanged)
    if (fetch_pc >= 0x0380A200 && fetch_pc < 0x0380A258) {
        printf("Pre-loop r0: 0x%08X at 0x%08X\n", cpu->registers[0], fetch_pc);
    }
    if (fetch_pc == 0x0380A258) {
        printf("STR target: 0x%08X (r0: 0x%08X, r2: 0x%08X)\n", 
               cpu->registers[0] + 1, cpu->registers[0], cpu->registers[2]);
    }
    if (fetch_pc == 0x0380A5F4) {
        printf("  r2: 0x%08X\n", cpu->registers[2]);
    }
    if (fetch_pc == 0x0380A268) {
        printf("  r1: 0x%08X, r7: 0x%08X, r8: 0x%08X\n", cpu->registers[1], cpu->registers[7], cpu->registers[8]);
    }
    if (fetch_pc == 0x0380A248 || fetch_pc == 0x0380A81C || fetch_pc == 0x03819454) {
        printf("  R0: 0x%08X, R2: 0x%08X, R3: 0x%08X, R5: 0x%08X, R8: 0x%08X, R10: 0x%08X, R14: 0x%08X, CPSR: 0x%08X\n",
               cpu->registers[0], cpu->registers[2], cpu->registers[3], cpu->registers[5], 
               cpu->registers[8], cpu->registers[10], cpu->registers[14], cpu->cpsr);
    }
    if (fetch_pc == 0x0380A250) {
        printf("Exiting Loop 1 at 0x0380A250, r3: 0x%08X, r5: 0x%08X\n", cpu->registers[3], cpu->registers[5]);
    }
    if ((fetch_pc >= 0x03800000 && fetch_pc <= 0x0380FFFF) || 
        (fetch_pc >= 0x00E00000 && fetch_pc <= 0x00E0FFFF)) {
        printf("Boot trace: PC: 0x%08X, r0: 0x%08X, opcode: 0x%08X\n", fetch_pc, cpu->registers[0], instr);
    }

    log_counter++;
    total_steps++;
    cpu->registers[15] += 4;

    if (total_steps >= 10000000) {
        printf("Stopped after 10000000 steps to limit log size (boot mode: %d)\n", cpu->mem->is_boot_mode);
        exit(1);
    }

    // Loop caps (unchanged)
    if (fetch_pc == 0x0380A5F4) {
        early_loop_count++;
        if (early_loop_count >= 5) {
            cpu->registers[15] = 0x0380A5F8;
            printf("Exited early loop at 0x0380A5F4 after 5 iterations\n");
            early_loop_count = 0;
        }
    }
    if (fetch_pc == 0x0380A5EC) {
        outer_loop_count++;
        if (outer_loop_count >= 10) {
            cpu->registers[15] = 0x0380A5F8;
            printf("Exited outer loop at 0x0380A5EC after 10 calls\n");
            outer_loop_count = 0;
        }
    }
    if (fetch_pc == 0x0380A248) {
        loop1_count++;
        if (loop1_count >= 5) {
            cpu->registers[15] = 0x0380A250;
            printf("Exited Loop 1 at 0x0380A248 after 5 iterations\n");
            loop1_count = 0;
            return;
        }
    }
    if (fetch_pc == 0x0380A268) {
        new_loop_count++;
        if (new_loop_count >= 5000) {
            cpu->registers[15] = 0x0380A26C;
            printf("Exited new loop at 0x0380A268 after 5000 iterations\n");
            new_loop_count = 0;
        }
    }
    if (fetch_pc == 0x0380A81C) {
        loop2_count++;
        if (loop2_count >= 5) {
            cpu->registers[15] = 0x0380A824;
            printf("Exited Loop 2 at 0x0380A81C after 5 iterations\n");
            loop2_count = 0;
            return;
        }
    }
    if (fetch_pc == 0x03819454) {
        loop3_count++;
        if (loop3_count >= 5) {
            cpu->registers[15] = 0x03819460;
            printf("Exited Loop 3 at 0x03819454 after 5 iterations\n");
            loop3_count = 0;
            return;
        }
    }

    uint32_t cond = (instr >> 28) & 0xF;
    if (!condition_met(cpu, cond)) {
        return;
    }

    if ((instr & 0x0C000000) == 0x00000000) { // Data Processing
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
            case 0x0: result = op1 & op2; break;
            case 0x1: result = op1 ^ op2; break;
            case 0x2: result = op1 - op2; overflow = ((op1 ^ result) & (~op2 ^ result)) >> 31; carry_out = (op1 >= op2); break;
            case 0x3: result = op2 - op1; overflow = ((op2 ^ result) & (~op1 ^ result)) >> 31; carry_out = (op2 >= op1); break;
            case 0x4: result = op1 + op2; overflow = ((op1 ^ result) & (op2 ^ result)) >> 31; carry_out = (result < op1); break;
            case 0x5: result = op1 + op2 + carry_in; overflow = ((op1 ^ result) & (op2 ^ result)) >> 31; carry_out = (result < op1) || (result == op1 && op2 != 0); break;
            case 0x6: result = op1 - op2 + carry_in - 1; overflow = ((op1 ^ result) & (~op2 ^ result)) >> 31; carry_out = (op1 >= op2) || (op1 == op2 && carry_in); break;
            case 0x7: result = op2 - op1 + carry_in - 1; overflow = ((op2 ^ result) & (~op1 ^ result)) >> 31; carry_out = (op2 >= op1) || (op2 == op1 && carry_in); break;
            case 0x8: result = op1 & op2; if (rd != 0) printf("Invalid TST with Rd != 0 at 0x%08X\n", fetch_pc); break;
            case 0x9: result = op1 ^ op2; if (rd != 0) printf("Invalid TEQ with Rd != 0 at 0x%08X\n", fetch_pc); break;
            case 0xA: result = op1 - op2; overflow = ((op1 ^ result) & (~op2 ^ result)) >> 31; carry_out = (op1 >= op2); if (rd != 0) printf("Invalid CMP with Rd != 0 at 0x%08X\n", fetch_pc); break;
            case 0xB: result = op1 + op2; overflow = ((op1 ^ result) & (op2 ^ result)) >> 31; carry_out = (result < op1); if (rd != 0) printf("Invalid CMN with Rd != 0 at 0x%08X\n", fetch_pc); break;
            case 0xC: result = op1 | op2; break;
            case 0xD: result = op2; break;
            case 0xE: result = op1 & ~op2; break;
            case 0xF: result = ~op2; break;
        }

        if (s_flag || opcode >= 0x8) {
            update_flags(cpu, result, op1, op2, carry_out, overflow);
        }
        if (opcode < 0x8 || opcode >= 0xC) {
            if (rd != 15) cpu->registers[rd] = result;
            else cpu->registers[15] = result & ADDR_MASK;
        }
    } else if ((instr & 0x0E000000) == 0x0A000000) { // Branch
        int32_t offset = instr & 0x00FFFFFF;
        if (offset & 0x00800000) offset |= 0xFF000000;
        offset <<= 2;
        uint32_t base_pc = fetch_pc + 8;
        uint32_t new_pc = base_pc + offset;
        int link = (instr >> 24) & 1;

        if (link) cpu->registers[14] = cpu->registers[15];
        cpu->registers[15] = new_pc & ADDR_MASK;
    } else if ((instr & 0x0C000000) == 0x04000000) { // Load/Store
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
            if (load) {
                if (byte) cpu->registers[rd] = memory_read_byte(cpu->mem, addr);
                else cpu->registers[rd] = memory_read_word(cpu->mem, addr);
            } else {
                if (byte) memory_write_byte(cpu->mem, addr, cpu->registers[rd]);
                else memory_write_word(cpu->mem, addr, cpu->registers[rd]);
            }
            if (writeback) cpu->registers[rn] = addr;
        } else {
            if (load) {
                if (byte) cpu->registers[rd] = memory_read_byte(cpu->mem, base);
                else cpu->registers[rd] = memory_read_word(cpu->mem, base);
            } else {
                if (byte) memory_write_byte(cpu->mem, base, cpu->registers[rd]);
                else memory_write_word(cpu->mem, base, cpu->registers[rd]);
            }
            cpu->registers[rn] = addr;
        }
        if (rd == 15) cpu->registers[15] &= ADDR_MASK;
    } else if ((instr & 0x0F000000) == 0x08000000) { // Block Data Transfer (LDM/STM)
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
                if (load) cpu->registers[i] = memory_read_word(cpu->mem, addr);
                else memory_write_word(cpu->mem, addr, cpu->registers[i]);
                addr += 4;
            }
        }
        if (writeback) cpu->registers[rn] = up ? base + (count * 4) : base - (count * 4);
        if (load && (reg_list & (1 << 15))) cpu->registers[15] &= ADDR_MASK;

        // Special case for LDMFD/STMFD with specific register lists
        if (instr == 0xE8BD0043) { // LDMFD sp!, {r0,r1,r6}
            cpu->registers[0] = memory_read_word(cpu->mem, cpu->registers[13]);
            cpu->registers[1] = memory_read_word(cpu->mem, cpu->registers[13] + 4);
            cpu->registers[6] = memory_read_word(cpu->mem, cpu->registers[13] + 8);
            cpu->registers[13] += 12; // Pop 3 words
        } else if (instr == 0xE92D0043) { // STMFD sp!, {r0,r1,r6}
            cpu->registers[13] -= 12; // Push 3 words
            memory_write_word(cpu->mem, cpu->registers[13], cpu->registers[0]);
            memory_write_word(cpu->mem, cpu->registers[13] + 4, cpu->registers[1]);
            memory_write_word(cpu->mem, cpu->registers[13] + 8, cpu->registers[6]);
        }
    } else if ((instr & 0x0FC000F0) == 0x00000090) { // Multiply
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
            update_flags(cpu, result, 0, 0, 0, 0);
        }
    } else if ((instr & 0x0F000000) == 0x0F000000) { // SWI
        cpu->spsr = cpu->cpsr;
        cpu->cpsr = (cpu->cpsr & ~PSR_MODE_MASK) | MODE_SVC | PSR_I;
        cpu->registers[14] = cpu->registers[15];
        cpu->registers[15] = 0x00000008 & ADDR_MASK;
        printf("SWI at 0x%08X, comment: 0x%06X\n", fetch_pc, instr & 0xFFFFFF);
    } else {
        printf("Unimplemented instruction 0x%08X at 0x%08X\n", fetch_pc, instr);
    }
}