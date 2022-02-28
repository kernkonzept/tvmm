/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "gic-v3.h"
#include "guest.h"

namespace Gic {

void Vcpu_handler::fetch_pending_irqs()
{
  // Atomically move newly pending Irqs here so that we can work on them
  // without having to bother about concurrent list modifications.
  Atomic_fwd_list<Irq> tmp;
  tmp.swap(_pending_irqs);

  // Move newly arrived pending IRQs to our own, sorted list. A remove-insert
  // sequence is not possible because there must be no point in time where a
  // pending&enabled IRQ is not on a list.
  for (auto n = tmp.begin(); n != tmp.end();)
    {
      auto pos = _owned_pend_irqs.before_begin();
      for (;;)
        {
          auto next = pos;
          ++next;
          if (next == _owned_pend_irqs.end() || next->prio() > n->prio())
            break;
          pos = next;
        }

      n = _owned_pend_irqs.move_after(pos, tmp, n);
    }

  // We could sort the list again if the guest changed the priorities. But
  // this overhead would be payed always which it not worth this corner case.
}

void
Dist_v3::setup_gic(Vmm::Guest *vmm)
{
  auto base        = Vmm::Guest_addr(CONFIG_TVMM_GIC_DIST_BASE);
  auto redist_base = Vmm::Guest_addr(CONFIG_TVMM_GIC_REDIST_BASE);

  vmm->add_mmio_device(
    Vmm::Region::ss(redist_base, 1UL << Redist::Stride, Vmm::Region_type::Virtual),
    &_redist);
  vmm->add_mmio_device(
    Vmm::Region::ss(base, 0x10000, Vmm::Region_type::Virtual),
    this);

  vmm->add_sys_reg_aarch64(3, 0, 12, 11, 5, &_sgir);
  vmm->add_sys_reg_aarch32_cp64(15, 0, 12, &_sgir);
}

} // Gic
