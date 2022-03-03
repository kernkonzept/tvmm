/*
 * Copyright (C) 2016 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */

#pragma once

#include <l4/sys/irq>
#include <l4/sys/cxx/ipc_epiface>

#include "generic_cpu_dev.h"
#include "irq.h"

namespace Vdev {

/**
 * Interrupt passthrough.
 *
 * Forwards L4Re interrupts to an Irq_sink.
 */
class Irq_svr : public Gic::Virq_handler
{
public:
  Irq_svr(Vmm::Generic_cpu_dev *cpu, L4::Cap<L4::Irq> irq,
          Gic::Ic *ic, unsigned dt_irq);
  ~Irq_svr();

  void handle_irq();
  void eoi() override;
  void set_priority(unsigned prio) override;

  void bind(L4::Cap<L4::Thread> thread, l4_umword_t cfg) override;
  void unbind() override;
  void enable() override;
  void disable() override;
  void set_pending() override;
  void clear_pending() override;

private:
  Vmm::Irq_sink _irq;
  L4::Cap<L4::Irq> _hw_irq;
};

} // namespace
