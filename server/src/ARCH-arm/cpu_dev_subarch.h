/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include <l4/sys/compiler.h>
#include <l4/sys/types.h>
#include <l4/sys/utcb.h>
#include <l4/sys/vcpu.h>
#include <l4/util/util.h>

#include "cpu_dev.h"

extern "C" {
  __attribute__((target("arm"))) void vcpu_entry(l4_vcpu_state_t *vcpu);
}

void vcpu_entry(l4_vcpu_state_t *_vcpu)
{
  // Save guest TPIDRURW and restore with our utcb address
  l4_umword_t guest_tpidrurw;
  asm volatile ("mrc    p15, 0, %0, c13, c0, 2 \n" : "=r"(guest_tpidrurw));
  l4_utcb_t *utcb = (l4_utcb_t *)l4_vcpu_e_info_user(_vcpu)[0];
  asm volatile ("mcr    p15, 0, %0, c13, c0, 2 \n" : : "r"(utcb));

  // Handle fault
  Vmm::Vcpu_ptr vcpu(_vcpu);
  Vmm::vcpu_entries[vcpu->r.err >> 26](vcpu);

  // Go back to guest
  vcpu.prepare_ipc_wait(utcb);
  l4_msgtag_t tag = prepare_guest_entry(vcpu);
  asm volatile ("mcr    p15, 0, %0, c13, c0, 2 \n" : : "r"(guest_tpidrurw));
  L4::Cap<L4::Thread>()->vcpu_resume_commit(tag, utcb);
}
