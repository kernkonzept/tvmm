/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#include "debug.h"
#include "guest.h"
#include "mmio_device.h"

namespace Vmm {

Mem_access Mmio_device::decode(l4_addr_t pfa, l4_addr_t offset, Vcpu_ptr vcpu)
{
  auto insn = vcpu.decode_mmio();

  if (insn.access == Vmm::Mem_access::Other)
    Dbg(Dbg::Mmio, Dbg::Warn, vcpu.get_vmm()->name())
      .printf("MMIO access @ 0x%lx: unknown instruction. Ignored.\n",
              pfa);
  else
    Dbg(Dbg::Mmio, Dbg::Trace, vcpu.get_vmm()->name())
      .printf("MMIO access @ 0x%lx (0x%lx) %s, width: %u\n",
              pfa, offset,
              insn.access == Vmm::Mem_access::Load ? "LOAD" : "STORE",
              (unsigned) insn.width);

  return insn;
}

}
