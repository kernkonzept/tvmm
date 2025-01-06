/*
 * Copyright (C) 2017, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include "debug.h"
#include "generic_cpu_dev.h"

namespace Vmm {

typedef void (*Entry)(Vmm::Vcpu_ptr vcpu);
extern Entry vcpu_entries[64];
l4_msgtag_t prepare_guest_entry(Vcpu_ptr vcpu);

class Guest;

class Cpu_dev
: public Generic_cpu_dev
{
public:
  enum
  {
    Flags_default_32 = 0x1d3,
    Flags_default_64 = 0x1c5,
    Flags_mode_32 = (1 << 4)
  };

  Cpu_dev(L4::Cap<L4::Thread> thread, Guest *vmm);

  void prepare_vcpu_startup(l4_addr_t entry);

  void startup() override;

  void reset() override;

  /**
   * Enter the virtual machine
   *
   * We assume an already setup register state that can be used as is
   * to enter the virtual machine (it was not changed by
   * vcpu_control_ext()). The virtualization related state is set to
   * default values, therefore we have to initialize this state here.
   */
  void L4_NORETURN start() override;

  l4_uint32_t affinity() const
  { return 0; }

private:
  enum
  {
    // define bits as 64 bit constants to make them usable in both
    // 32/64 contexts
    Mpidr_mp_ext    = 1ULL << 31,
    Mpidr_up_sys    = 1ULL << 30,
    Mpidr_mt_sys    = 1ULL << 24,
    // Affinity Aff{0,1,2} in [23-0], Aff3 in [39-32]
    Mpidr_aff_mask  = (0xffULL << 32) | 0xffffffULL,
  };

  l4_umword_t _ip;
  l4_umword_t _flags;
};

}
