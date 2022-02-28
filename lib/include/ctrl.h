/*
 * Copyright (C) 2023-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
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

  /**
   * Query VM status.
   */
  L4_INLINE_RPC(long, status, (Status &status));

  /**
   * Query used vCPU execution time.
   */
  L4_INLINE_RPC(long, vcpu_time, (l4_kernel_clock_t &us));

  /**
   * Suspend a running VM.
   *
   * Does nothing if the VM is Shutdown or Crashed.
   */
  L4_INLINE_RPC(long, suspend, (), L4::Ipc::Call_t<L4_CAP_FPAGE_RW>);

  /**
   * Resume VM execution.
   *
   * The VM must be in Stopped state. Otherwise the call has no effect.
   */
  L4_INLINE_RPC(long, resume, (), L4::Ipc::Call_t<L4_CAP_FPAGE_RW>);

  /**
   * Reset the VM state.
   *
   * This will reset the VM vCPU to the entry point and re-initialize all
   * virtual devices. Additionally, if the VM was in Shutdown or Crashed state,
   * it will be set in Stopped state.
   */
  L4_INLINE_RPC(long, reset, (), L4::Ipc::Call_t<L4_CAP_FPAGE_RW>);

  typedef L4::Typeid::Rpcs<name_t, status_t, vcpu_time_t, suspend_t, resume_t,
                           reset_t> Rpcs;
};

}
