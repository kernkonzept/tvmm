/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "debug.h"
#include "irq_svr.h"

namespace Vdev {

Irq_svr::Irq_svr(Vmm::Generic_cpu_dev *cpu, L4::Cap<L4::Irq> irq,
                 Gic::Ic *ic, unsigned dt_irq)
: _hw_irq_cap(irq), _hw_irq_cfg(dt_irq), _vcpu_thread(cpu->thread_cap()),
  _active(false), _enabled(false)
{
  if (ic->get_eoi_handler(dt_irq))
    Fatal().abort("Bind IRQ for Irq_svr object.");

  // Only support direct injection. We just point the virtual GIC to this
  // instance for the vIRQ. Once the guest enables the interrupt, it will be
  // bound.
  ic->bind_virq_handler(dt_irq, this);
}

Irq_svr::~Irq_svr()
{}

void Irq_svr::eoi()
{
  // Called if an active Irq was abandoned (detach() return 2) and the guest
  // has now EOIed. We can now rebind it to the vCPU again.
  _active = false;
  if (_enabled)
      {
        if (l4_error(_hw_irq_cap->bind_vcpu(_vcpu_thread, _hw_irq_cfg)) < 0)
          Fatal().abort("EOI Bind irq to vCPU");
        _hw_irq_cap->unmask();
      }
}

void Irq_svr::set_priority(unsigned prio)
{
#ifdef IRQ_PRIORITIES_NOT_IMPLEMENTED_YET
  _hw_irq_cap->set_priority(prio);
#else
  static_cast<void>(prio);
#endif
}

void Irq_svr::configure(l4_umword_t cfg)
{
  _hw_irq_cfg = cfg;

  if (_enabled && !_active)
    if (l4_error(_hw_irq_cap->bind_vcpu(_vcpu_thread, _hw_irq_cfg)) < 0)
      Fatal().abort("Configure vIRQ");
}

void Irq_svr::enable()
{
  if (_enabled)
    return;

  _enabled = true;
  if (!_active)
    {
      if (l4_error(_hw_irq_cap->bind_vcpu(_vcpu_thread, _hw_irq_cfg)) < 0)
        Fatal().abort("Enable vIRQ");
      _hw_irq_cap->unmask();
    }
}

void Irq_svr::disable()
{
  if (!_enabled)
    return;

  _enabled = false;
  int err = l4_error(_hw_irq_cap->detach());
  if (err < 0)
    Fatal().abort("Disable vIRQ");

  // If the vIRQ was active, the kernel has abandoned the Irq and we will
  // eventually get an EOI by the guest. The eoi() handler will re-attach the
  // Irq if still enabled.
  _active = err == 2;
}

void Irq_svr::set_pending()
{ _hw_irq_cap->trigger(); }

void Irq_svr::clear_pending()
{ /* not implemented */ }


}
