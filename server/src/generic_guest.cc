/*
 * Copyright (C) 2015, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "generic_guest.h"

namespace Vmm {

Generic_guest::~Generic_guest()
{}

int Generic_guest::handle_mmio(l4_addr_t pfa, Vcpu_ptr vcpu)
{
  auto insn = vcpu.decode_mmio();

  if (insn.access != Vmm::Mem_access::Other)
    {
      auto f = _memmap.find(Region(Guest_addr(pfa)));
      if (f != _memmap.end())
	{
	  Region region = f->first;
	  Mmio_device *device = f->second;

	  return device->access(pfa, pfa - region.start.get(),
				vcpu, _task,
				region.start.get(), region.end.get());
	}

    }
  else
    pfa = ~0UL;

#if defined(CONFIG_TVMM_FAULT_HALT)
  warn()
#else
  info()
#endif
        .printf("Invalid %s 0x%lx, ip 0x%lx! %s...\n",
                insn.access == Vmm::Mem_access::Load
                  ? "load from"
                  : (insn.access == Vmm::Mem_access::Store ? "store to"
                                                           : "access at"),
                pfa, vcpu->r.ip,
#if defined(CONFIG_TVMM_FAULT_HALT)
		"Halting"
#elif defined(CONFIG_TVMM_FAULT_IGNORE)
		"Ignoring"
#elif defined(CONFIG_TVMM_FAULT_INJECT)
		"Injecting"
#endif
                );

#if defined(CONFIG_TVMM_FAULT_HALT)
  return -L4_EFAULT;
#elif defined(CONFIG_TVMM_FAULT_IGNORE)
  if (insn.access == Vmm::Mem_access::Load)
    {
      insn.value = 0;
      vcpu.writeback_mmio(insn);
    }
  return Jump_instr;
#elif defined(CONFIG_TVMM_FAULT_INJECT)
  if (inject_abort(pfa, vcpu))
    return Retry;
  warn().printf("Abort inject failed! Halting VM...\n");
  return -L4_EFAULT;
#else
#error "Unsupported fault mode"
#endif
}

} // namespace
