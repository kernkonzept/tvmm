/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
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
 * Forwards L4Re interrupts to the guest.
 */
class Irq_svr : public Gic::Virq_handler
{
public:
  Irq_svr(Vmm::Generic_cpu_dev *cpu, L4::Cap<L4::Irq> irq,
          Gic::Ic *ic, unsigned dt_irq);
  ~Irq_svr();

  void eoi() override;
  void set_priority(unsigned prio) override;

  void configure(l4_umword_t cfg) override;
  void enable() override;
  void disable() override;
  void set_pending() override;
  void clear_pending() override;

private:
  L4::Cap<L4::Irq> _hw_irq_cap;
  l4_umword_t _hw_irq_cfg;
  L4::Cap<L4::Thread> _vcpu_thread;
  bool _active : 1;
  bool _enabled : 1;
};

} // namespace
