/*
 * Copyright (c) 2025 Nikita Ukhrenkov <thekit@disroot.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include "logging.h"

/* AArch64 instruction opcodes and system register encodings */
#define MRS_OPCODE 0xD53
#define TPIDR_EL0  0x5E82
#define B_OPCODE   0x14      /* Unconditional branch */

/* Thunk size in bytes (4 instructions = 16 bytes) */
#define THUNK_SIZE 16

/* Instruction format for MRS X<rt>, TPIDR_EL0 */
struct __attribute__((__packed__)) mrs_inst {
    uint32_t rt:5;        /* 0:4   - Destination register */
    uint32_t sys_reg:15;  /* 5:19  - System register encoding */
    uint32_t opcode:12;   /* 20:31 - Must be 0xD53 (MRS instruction) */
};
_Static_assert(sizeof(struct mrs_inst) == 4, "MRS instruction size must be 4");

/* Instruction format for B <offset> (unconditional branch) */
struct __attribute__((__packed__)) branch_inst {
    uint32_t imm26:26;    /* 0:25  - 26-bit signed immediate (word offset) */
    uint32_t opcode:6;    /* 26:31 - Must be 0x05 (B instruction) */
};
_Static_assert(sizeof(struct branch_inst) == 4, "Branch instruction size must be 4");

/* Calculate the branch offset from source to target (in words) */
static int32_t calculate_branch_offset(void* source, void* target) {
    intptr_t diff = (intptr_t)target - (intptr_t)source;

    /* Branch offset is in words (4-byte units) */
    if (diff % 4 != 0) {
        return 0x7FFFFFFF; /* Invalid offset */
    }

    int32_t word_offset = diff / 4;

    /* Check if offset fits in 26-bit signed immediate */
    if (word_offset >= -(1 << 25) && word_offset < (1 << 25)) {
        return word_offset;
    }

    return 0x7FFFFFFF; /* Out of range */
}

/* Generate a thunk that adjusts the thread pointer and jumps back */
static void* generate_tls_thunk(int tls_offset_words, uint32_t target_register, void* mrs_location) {
    void* thunk = hybris_allocate_thunk_near(mrs_location, THUNK_SIZE);
    if (!thunk) {
        HYBRIS_DEBUG_LOG(HOOKS, "Failed to allocate thunk for TLS patching");
        return NULL;
    }

    uint32_t* code = (uint32_t*)thunk;

    /* Convert word offset to byte offset */
    int tls_offset_bytes = tls_offset_words * 8;

    /* Calculate return address - the instruction after the original MRS */
    void* return_addr = (char*)mrs_location + 4;

    /* Calculate branch offset back to the return address */
    int32_t return_offset = calculate_branch_offset((char*)thunk + 8, return_addr);
    if (return_offset == 0x7FFFFFFF) {
        HYBRIS_DEBUG_LOG(HOOKS, "Return address %p too far from thunk %p", return_addr, thunk);
        return NULL;
    }

    /* Generate thunk code:
     * mrs x<rt>, tpidr_el0     ; Read thread pointer
     * add x<rt>, x<rt>, #offset ; Add TLS offset
     * b <return_addr>          ; Branch back to next instruction
     * nop                      ; Padding to 16 bytes
     */

    /* MRS X<rt>, TPIDR_EL0 */
    code[0] = 0xD53BD040 | target_register;

    /* ADD X<rt>, X<rt>, #immediate */
    if (tls_offset_bytes >= 0 && tls_offset_bytes <= 0xFFF) {
        /* ADD X<rt>, X<rt>, #imm (12-bit immediate) */
        code[1] = 0x91000000 | (target_register << 5) | target_register | (tls_offset_bytes << 10);
    } else {
        /* For larger offsets, we'd need to use multiple instructions */
        HYBRIS_DEBUG_LOG(HOOKS, "TLS offset %d too large for simple ADD instruction", tls_offset_bytes);
        return NULL;
    }

    /* B <return_offset> - Branch back to the instruction after the original MRS */
    code[2] = 0x14000000 | (return_offset & 0x3FFFFFF);

    /* NOP (padding) */
    code[3] = 0xD503201F;

    HYBRIS_DEBUG_LOG(HOOKS, "Generated TLS thunk at %p for register X%d with offset %d, returns to %p",
                     thunk, target_register, tls_offset_words, return_addr);

    return thunk;
}

/* Replace MRS instruction with branch to thunk */
static int patch_mrs_with_branch(uint32_t* mrs_location, void* thunk) {
    int32_t offset = calculate_branch_offset(mrs_location, thunk);

    if (offset == 0x7FFFFFFF) {
        HYBRIS_DEBUG_LOG(HOOKS, "Thunk %p too far from MRS at %p for branch instruction",
                         thunk, mrs_location);
        return 0;
    }

    /* Create branch instruction: B <offset> */
    struct branch_inst branch = {
        .imm26 = offset & 0x3FFFFFF,
        .opcode = 0x05
    };

    /* Replace the MRS instruction */
    *mrs_location = *(uint32_t*)&branch;

    HYBRIS_DEBUG_LOG(HOOKS, "Patched MRS at %p with branch to thunk %p (offset: %d)",
                     mrs_location, thunk, offset);

    return 1;
}

void hybris_patch_tls_arch(void* segment_addr, size_t segment_size, int tls_offset) {
    uint32_t* text = (uint32_t*)segment_addr;
    size_t count = segment_size / sizeof(uint32_t);
    int patches_applied = 0;

    HYBRIS_DEBUG_LOG(HOOKS, "Scanning %zu instructions for MRS TPIDR_EL0 in segment %p",
                     count, segment_addr);

    for (size_t i = 0; i < count; i++) {
        const struct mrs_inst* mrs = (const struct mrs_inst*)&text[i];

        /* Look for MRS instruction that reads TPIDR_EL0 */
        if (mrs->opcode != MRS_OPCODE || mrs->sys_reg != TPIDR_EL0) {
            continue;
        }

        HYBRIS_DEBUG_LOG(HOOKS, "Found MRS TPIDR_EL0, X%d at offset 0x%zx",
                         mrs->rt, i * 4);

        /* Generate thunk for this specific register */
        void* thunk = generate_tls_thunk(tls_offset, mrs->rt, &text[i]);
        if (!thunk) {
            continue;
        }

        /* Replace MRS with branch to thunk */
        if (patch_mrs_with_branch(&text[i], thunk)) {
            patches_applied++;
        }
    }

    if (patches_applied > 0) {
        /* Flush instruction cache for the entire segment */
        __builtin___clear_cache(segment_addr, (char*)segment_addr + segment_size);

        HYBRIS_DEBUG_LOG(HOOKS, "Applied %d TLS patches in segment %p",
                         patches_applied, segment_addr);
    }
}
