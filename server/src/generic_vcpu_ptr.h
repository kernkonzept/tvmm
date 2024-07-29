/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <assert.h>
#include <stdio.h>

#include <l4/re/rm>
#include <l4/sys/thread>
#include <l4/sys/vcpu.h>
#include <l4/re/util/br_manager>
#include <l4/re/util/object_registry>

#include "debug.h"

namespace Vmm {

class Guest;

class Generic_vcpu_ptr
{
public:
  l4_vcpu_state_t *operator -> () const noexcept
  { return _s; }

  l4_vcpu_state_t *operator * () const noexcept
  { return _s; }

  void control_ext(L4::Cap<L4::Thread> thread)
  {
    if (l4_error(thread->vcpu_control_ext((l4_addr_t)_s)))
      Fatal().abort("Could not create vCPU. "
                    "Running virtualization-enabled kernel?\n");

    if (!l4_vcpu_check_version(_s))
      Fatal().abort("FATAL: Could not create vCPU. "
                    "vCPU interface mismatch\n");

    trace().printf("VCPU mapped @ %p and enabled\n", _s);
  }

  Guest* get_vmm() const
  { return reinterpret_cast<Guest *>(_s->user_data[Reg_vmm_ptr]); }

  void set_vmm(Guest *vmm)
  { _s->user_data[Reg_vmm_ptr] = reinterpret_cast<l4_umword_t>(vmm); }

  L4Re::Util::Object_registry *get_ipc_registry() const
  { return reinterpret_cast<L4Re::Util::Object_registry *>(_s->user_data[Reg_ipc_registry]); }

  void set_ipc_registry(L4Re::Util::Object_registry *registry)
  { _s->user_data[Reg_ipc_registry] = reinterpret_cast<l4_umword_t>(registry); }

  L4Re::Util::Br_manager *get_ipc_bm() const
  { return reinterpret_cast<L4Re::Util::Br_manager *>(_s->user_data[Reg_ipc_bm]); }

  void set_ipc_bm(L4Re::Util::Br_manager *bm)
  { _s->user_data[Reg_ipc_bm] = reinterpret_cast<l4_umword_t>(bm); }

  void handle_ipc(l4_msgtag_t tag, l4_umword_t label, l4_utcb_t *utcb)
  {
    l4_msgtag_t r = get_ipc_registry()->dispatch(tag, label, utcb);
    if (r.label() != -L4_ENOREPLY)
      l4_ipc_send(L4_INVALID_CAP | L4_SYSF_REPLY, utcb, r,
                  L4_IPC_SEND_TIMEOUT_0);
  }

  bool wait_for_ipc(l4_utcb_t *utcb, l4_timeout_t to)
  {
    prepare_ipc_wait(utcb);

    l4_umword_t src;
    l4_msgtag_t tag = l4_ipc_wait(utcb, &src, to);
    if (!tag.has_error())
      handle_ipc(tag, src, utcb);

    return !tag.has_error();
  }

  void process_pending_ipc(l4_utcb_t *utcb)
  {
    while (_s->sticky_flags & L4_VCPU_SF_IRQ_PENDING)
      wait_for_ipc(utcb, L4_IPC_BOTH_TIMEOUT_0);
  }

  void prepare_ipc_wait(l4_utcb_t *utcb)
  {
    get_ipc_bm()->setup_wait(utcb, L4::Ipc_svr::Reply_separate);
  }

protected:
  enum User_data_regs
  {
    Reg_vmm_ptr = 0,
    Reg_ipc_registry,
    Reg_ipc_bm,
    Reg_arch_base
  };

  Dbg warn()
  { return Dbg(Dbg::Cpu, Dbg::Warn, get_vmm_name()); }

  Dbg info()
  { return Dbg(Dbg::Cpu, Dbg::Info, get_vmm_name()); }

  Dbg trace()
  { return Dbg(Dbg::Cpu, Dbg::Trace, get_vmm_name()); }

  static_assert(Reg_arch_base <= 7, "Too many user_data registers used");

  explicit Generic_vcpu_ptr(l4_vcpu_state_t *s) : _s(s) {}

  static l4_uint64_t reg_extend_width(l4_uint64_t value, char size, bool signext)
  {
    if (signext)
      {
        switch (size)
          {
          case 0: return (l4_uint64_t)((l4_int8_t)value);
          case 1: return (l4_uint64_t)((l4_int16_t)value);
          case 2: return (l4_uint64_t)((l4_int32_t)value);
          default: return value;
          }
      }

    switch (size)
      {
      case 0: return (l4_uint64_t)((l4_uint8_t)value);
      case 1: return (l4_uint64_t)((l4_uint16_t)value);
      case 2: return (l4_uint64_t)((l4_uint32_t)value);
      default: return value;
      }
  }

  l4_vcpu_state_t *_s;

private:
  char const *get_vmm_name();
};

}
