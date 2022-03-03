/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */
#pragma once

#include <l4/sys/capability>
#include <l4/sys/cxx/ipc_iface>
#include <l4/sys/icu>

namespace Tvmm {

class Ctrl : public L4::Kobject_t<Ctrl, L4::Icu>
{
public:
  enum Status
  {
    Running,
    Stopped,
    Shutdown,
    Crashed,
    Unknown,
  };

  /**
   * Get VM name.
   */
  L4_INLINE_RPC(long, name, (L4::Ipc::Array<char> &name));

  L4_INLINE_RPC(long, status, (Status &status));

  L4_INLINE_RPC(long, vcpu_time, (l4_kernel_clock_t &us));

  L4_INLINE_RPC(long, suspend, (), L4::Ipc::Call_t<L4_CAP_FPAGE_RW>);

  L4_INLINE_RPC(long, resume, (), L4::Ipc::Call_t<L4_CAP_FPAGE_RW>);

  L4_INLINE_RPC(long, reset, (), L4::Ipc::Call_t<L4_CAP_FPAGE_RW>);

  typedef L4::Typeid::Rpcs<name_t, status_t, vcpu_time_t, suspend_t, resume_t,
                           reset_t> Rpcs;
};

}
