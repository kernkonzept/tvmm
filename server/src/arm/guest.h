/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *            Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */
#pragma once

#include <l4/cxx/avl_map>
#include <l4/sys/vm>

#include "core_timer.h"
#include "generic_guest.h"
#include "gic-v3.h"
#include "smccc_device.h"
#include "sys_reg.h"
#include "cpu_dev.h"
#include "loader.h"

namespace Vmm {

/**
 * ARM virtual machine monitor.
 */
class Guest : public Generic_guest
{
public:
  Guest(L4::Cap<L4::Vm> task, char const *name);

  void set_irq_priority_range(unsigned min, unsigned max)
  { _gic.set_irq_priority_range(min, max); }

  void run(Cpu_dev *cpu);

  l4_msgtag_t handle_entry(Vcpu_ptr vcpu);

  Gic::Ic *gic() { return &_gic; }
  Gic::Ic const *gic() const { return &_gic; }

  void wait_for_timer_or_irq(Vcpu_ptr vcpu);

  enum Smccc_method
  {
    Smc,
    Hvc,
    Num_smcc_methods
  };

  enum { Num_vm_handlers = 2 };

  void register_vm_handler(Smccc_method method,
                           Vmm::Smccc_device* handler)
  {
    for (unsigned i = 0; i < Num_vm_handlers; i++)
      {
        if (!_smccc_handlers[method][i])
          {
            _smccc_handlers[method][i] = handler;
            return;
          }
      }

    Err().printf("Too many scmm handlers!\n");
    abort();
  }

  void handle_smccc_call(Vcpu_ptr vcpu, Smccc_method method)
  {
    bool res = false;
    // Check if this is a valid/supported SMCCC call
    if (Smccc_device::is_valid_call(vcpu->r.r[0]))
      {
        unsigned imm = vcpu.hsr().svc_imm();
        for (auto const &h: _smccc_handlers[method])
          if ((res = h->vm_call(imm, vcpu)))
            break;
      }

    if (!res)
      {
        warn().printf("No handler for %s call: imm=%x a0=%lx a1=%lx ip=%lx "
                      "lr=%lx\n",
                      (method == Smc) ? "SMC" : "HVC",
                      static_cast<unsigned>(vcpu.hsr().svc_imm()),
                      vcpu->r.r[0], vcpu->r.r[1],
                      vcpu->r.ip, vcpu.get_lr());
        vcpu->r.r[0] = Smccc_device::Not_supported;
      }

    vcpu->r.ip += 4;
  }

  void handle_wfx(Vcpu_ptr vcpu);
  void handle_ppi(Vcpu_ptr vcpu);

  void handle_ex_regs_exception(Vcpu_ptr vcpu);

  bool inject_abort(Vcpu_ptr vcpu, bool inst, l4_addr_t addr);
  bool inject_undef(Vcpu_ptr vcpu);

  using Sys_reg = Vmm::Arm::Sys_reg;

  Sys_reg *sys_reg(Sys_reg::Key k)
  {
    return _sys_regs.find(k);
  }

  void add_sys_reg_aarch32(unsigned cp, unsigned op1,
                           unsigned crn, unsigned crm,
                           unsigned op2,
                           Sys_reg *r)
  {
    _sys_regs[Sys_reg::Key::cp_r(cp, op1, crn, crm, op2)] = r;
  }

  void add_sys_reg_aarch32_cp64(unsigned cp, unsigned op1,
                                unsigned crm,
                                Sys_reg *r)
  {
    _sys_regs[Sys_reg::Key::cp_r_64(cp, op1, crm)] = r;
  }

  void add_sys_reg_aarch64(unsigned op0, unsigned op1,
                           unsigned crn, unsigned crm,
                           unsigned op2,
                           Sys_reg *r);

  void add_sys_reg_both(unsigned op0, unsigned op1,
                        unsigned crn, unsigned crm,
                        unsigned op2,
                        Sys_reg *r)
  {
    add_sys_reg_aarch64(op0, op1, crn, crm, op2, r);
    // op0 == 3 -> cp15, op0 == 2 -> cp14
    add_sys_reg_aarch32(op0 + 12, op1, crn, crm, op2, r);
  }

  void suspend();
  void resume();
  void reset();

  void load_elf(unsigned long elf_addr, Cpu_dev *cpu);

protected:
  bool inject_abort(l4_addr_t addr, Vcpu_ptr vcpu) override;

  unsigned long _load_elf();

private:
  Gic::Dist_v3 _gic;
  Vdev::Core_timer _timer;
  Cpu_dev *_cpu;
  bool guest_64bit = false;
  Loader::Elf_binary _elf;

  Vmm::Smccc_device *_smccc_handlers[Num_smcc_methods][Num_vm_handlers] = {};

  template<unsigned N>
  class Sys_reg_map
  {
    struct {
      Sys_reg::Key k;
      Sys_reg *v;
    } _regs[N] = {};
    unsigned _num = 0;

  public:
    Sys_reg *find(Sys_reg::Key key) const
    {
      for (unsigned i = 0; i < _num; i++)
        {
          if (_regs[i].k == key)
            return _regs[i].v;
        }

      return nullptr;
    }

    Sys_reg *&operator[](Sys_reg::Key key)
    {
      for (unsigned i = 0; i < _num; i++)
        {
          if (_regs[i].k == key)
            return _regs[i].v;
        }

      return _regs[_num++].v;
    }
  };

  Sys_reg_map<8> _sys_regs;
};

} // namespace
