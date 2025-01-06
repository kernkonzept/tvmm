/*
 * Copyright (C) 2017, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "cpu_dev.h"
#include "cpu_dev_subarch.h"
#include "arm_hyp.h"
#include "guest.h"

#include <l4/util/util.h>
#include <l4/sys/ipc.h>

namespace Vmm {

Cpu_dev::Cpu_dev(L4::Cap<L4::Thread> thread, Guest *vmm)
: Generic_cpu_dev(thread, vmm)
{}

void
Cpu_dev::prepare_vcpu_startup(l4_addr_t entry)
{
  if (false /*Guest_64bit_supported && guest_64bit */)
    _flags = Cpu_dev::Flags_default_64;
  else
    {
      _flags = Cpu_dev::Flags_default_32;
      if (entry & 1)
        {
          // set thumb mode, remove thumb bit from address
          _flags |= 1 << 5;
          entry &= ~1U;
        }
    }

  _ip = entry;
}

void
Cpu_dev::startup()
{
  Generic_cpu_dev::startup();

  // entry_sp is derived from thread local stack pointer
  asm volatile ("mov %0, sp" : "=r"(_vcpu->entry_sp));
  _vcpu->entry_ip = (l4_umword_t) &vcpu_entry;

  reset();
}

void
Cpu_dev::reset()
{
  _vcpu->r.ip = _ip;
  _vcpu->r.sp = 0;
  _vcpu->r.flags = _flags;

  //
  // initialize hardware related virtualization state
  //
  Vmm::Arm::Gic_h::init_vcpu(*_vcpu);

  // we set FB, and BSU to inner sharable to tolerate migrations
  l4_umword_t hcr = 0x30023f; // VM, PTW, AMO, IMO, FMO, FB, SWIO, TIDCP, TAC
  hcr |= 1UL << 10; // BUS = inner sharable
  hcr |= 3UL << 13; // Trap WFI and WFE
  l4_vcpu_e_write(*_vcpu, L4_VCPU_E_HCR, hcr);

  // set C, I, CP15BEN
  // set TE if guest entry is in thumb mode
  l4_vcpu_e_write_32(*_vcpu, L4_VCPU_E_SCTLR,
    ((_vcpu->r.flags & (1UL << 5)) ? (1UL << 30) : 0)
    | (1UL << 5) | (1UL << 2) | (1UL << 12));

  // The type of vmpidr differs between ARM32 and ARM64, so we use 64
  // bit here as a superset.
  l4_uint64_t vmpidr = l4_vcpu_e_read(*_vcpu, L4_VCPU_E_VMPIDR);

  if (! (vmpidr &  Mpidr_mp_ext))
    Dbg(Dbg::Cpu, Dbg::Info, _vcpu.get_vmm()->name())
      .printf("Vmpidr: %llx - Missing multiprocessing extension\n", vmpidr);

  // remove mt/up bit and replace affinity with value from device tree
  l4_vcpu_e_write(*_vcpu, L4_VCPU_E_VMPIDR,
                  (vmpidr & ~(Mpidr_up_sys | Mpidr_mt_sys | Mpidr_aff_mask)));

  arm_subarch_setup(*_vcpu, !(_vcpu->r.flags & Flags_mode_32));

  //
  // Initialize vcpu state
  //
  _vcpu->saved_state = L4_VCPU_F_FPU_ENABLED
    | L4_VCPU_F_USER_MODE
    | L4_VCPU_F_IRQ
    | L4_VCPU_F_PAGE_FAULTS
    | L4_VCPU_F_EXCEPTIONS;
}

void
Cpu_dev::start()
{
  l4_uint64_t vmpidr = l4_vcpu_e_read(*_vcpu, L4_VCPU_E_VMPIDR);
  Dbg(Dbg::Cpu, Dbg::Info, _vcpu.get_vmm()->name())
    .printf("Starting Cpu @ 0x%lx in %dBit mode (handler @ %lx,"
            " stack: %lx, task: %lx, mpidr: %llx (orig: %llx)\n",
            _vcpu->r.ip,
            _vcpu->r.flags & Flags_mode_32 ? 32 : 64,
            _vcpu->entry_ip, _vcpu->entry_sp, _vcpu->user_task,
            static_cast<l4_uint64_t>(l4_vcpu_e_read(*_vcpu, L4_VCPU_E_VMPIDR)),
            vmpidr);

  _vcpu.prepare_ipc_wait(l4_utcb());

  L4::Cap<L4::Thread> myself;
  auto res = myself->vcpu_resume_commit(myself->vcpu_resume_start());

  // Could not enter guest! Take us offline...
  Err().printf("vcpu_resume_commit error %lx\n", l4_error(res));
  l4_sleep_forever();
}

}
