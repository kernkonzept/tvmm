/*
 * Copyright (C) 2023-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/re/util/icu_svr>
#include <l4/sys/cxx/ipc_epiface>
#include <l4/sys/thread>

#include <l4/tvmm/ctrl.h>
#include "guest.h"

class Vm_ctrl
: public L4Re::Util::Icu_cap_array_svr<Vm_ctrl>,
  public L4::Epiface_t<Vm_ctrl, Tvmm::Ctrl>,
  public Vmm::Guest::State_listener
{
  typedef L4Re::Util::Icu_cap_array_svr<Vm_ctrl> Icu_svr;

  Vmm::Guest *_guest;
  Icu_svr::Irq _irq;

public:
  Vm_ctrl(Vmm::Guest *guest)
  : Icu_svr(1, &_irq), _guest(guest)
  {
    guest->set_state_listener(this);
  }

  // Vmm::Guest::State_listener
  void state_change() override
  {
    _irq.trigger();
  }

  // Tvmm::Ctrl

  long op_name(Tvmm::Ctrl::Rights, L4::Ipc::Array_ref<char> &name)
  {
    unsigned len = strlen(_guest->name());
    if (len > name.length)
      len = name.length;

    memcpy(name.data, _guest->name(), len);
    name.length = len;

    return len;
  }

  long op_status(Tvmm::Ctrl::Rights, Tvmm::Ctrl::Status &status)
  {
    status = (Tvmm::Ctrl::Status)_guest->state();
    return L4_EOK;
  }

  long op_vcpu_time(Tvmm::Ctrl::Rights, l4_kernel_clock_t &us)
  {
    L4::Cap<L4::Thread> self;
    return l4_error(self->stats_time(&us));
  }

  long op_suspend(Tvmm::Ctrl::Rights)
  {
    _guest->suspend();
    return L4_EOK;
  }

  long op_resume(Tvmm::Ctrl::Rights)
  {
    _guest->resume();
    return L4_EOK;
  }

  long op_reset(Tvmm::Ctrl::Rights)
  {
    _guest->reset();
    return L4_EOK;
  }
};
