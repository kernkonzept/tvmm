/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */

#include "debug.h"
#include "irq_svr.h"

namespace Vdev {

Irq_svr::Irq_svr(Vmm::Generic_cpu_dev *cpu, L4::Cap<L4::Irq> irq,
                 Gic::Ic *ic, unsigned dt_irq)
: _hw_irq(irq)
{
  if (ic->get_eoi_handler(dt_irq))
    Fatal().abort("Bind IRQ for Irq_svr object.");

  // Point irq_svr to ic:dt_irq for upstream events (like
  // interrupt delivery)
  _irq.rebind(ic, dt_irq);

  // Try direct injection. If this fails bind ourself and route through vmm
  // as usual...
  if (l4_error(_hw_irq->bind_vcpu(cpu->thread_cap(), dt_irq)) < 0)
    Fatal().abort("Bind irq to vCPU");
  _irq.set_virq_handler(this);
}

Irq_svr::~Irq_svr()
{}

void Irq_svr::handle_irq()
{
  _irq.inject();
}

void Irq_svr::eoi()
{
  _irq.ack();
  _hw_irq->unmask();
}

void Irq_svr::set_priority(unsigned prio)
{
  _hw_irq->set_priority(prio);
}

void Irq_svr::bind(L4::Cap<L4::Thread> thread, l4_umword_t cfg)
{
  if (l4_error(_hw_irq->bind_vcpu(thread, cfg)) < 0)
    Fatal().abort("bind_vcpu");
}

void Irq_svr::unbind()
{ _hw_irq->detach(); }

void Irq_svr::enable()
{ _hw_irq->unmask_vcpu(); }

void Irq_svr::disable()
{ _hw_irq->mask_vcpu(); }

void Irq_svr::set_pending()
{ _hw_irq->trigger(); }

void Irq_svr::clear_pending()
{ _hw_irq->clear_vcpu(); }


}
