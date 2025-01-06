/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2019-2020, 2024 Kernkonzept GmbH.
 * Author(s): Christian Pötzsch <christian.poetzsch@kernkonzept.com>
 *
 */
#pragma once

#include "smccc_device.h"

namespace {

class Vm_print_device : public Vmm::Smccc_device
{
  enum Vm_print_error_codes
  {
    Success = 0,
  };

public:
  bool vm_call(unsigned imm, Vmm::Vcpu_ptr vcpu) override
  {
    if (imm != 1)
      return false;

    if (!is_valid_func_id(vcpu->r.r[0]))
      return false;

    putchar(vcpu->r.r[1]);
    vcpu->r.r[0] = Success;

    return true;
  }

private:
  bool is_valid_func_id(l4_umword_t reg) const
  {
    // Check for the correct SMC calling convention:
    // - this must be a fast call (bit 31)
    // - it is within the uvmm range (bits 29:24)
    // - the rest must be zero
    return (reg & 0xbfffffff) == 0x86000000;
  }
};

}
