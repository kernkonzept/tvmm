/*
 * Copyright (C) 2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include <l4/sys/thread.h>
#include <l4/re/elf_aux.h>
#include "guest.h"

L4RE_ELF_AUX_ELEM_T(l4re_elf_aux_mword_t, __ex_regs_flags,
                    L4RE_ELF_AUX_T_EX_REGS_FLAGS,
                    L4_THREAD_EX_REGS_ARM_SET_EL_EL1);

// Override the syscall symbol from the l4sys library. Relies on the ELF
// linking behaviour which ignores symbols from libraries that are already
// defined by the program or some other library before (in link order).
asm (
  ".arch_extension virt\n"
  ".global __l4_sys_syscall\n"
  ".type __l4_sys_syscall, #function\n"
  "__l4_sys_syscall:\n"
  "   hvc #0\n"
  "   bx lr\n"
);
