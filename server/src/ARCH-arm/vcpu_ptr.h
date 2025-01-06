/*
 * Copyright (C) 2015-2021, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <assert.h>

#include "aarch32_hyp.h"
#include "generic_vcpu_ptr.h"
#include "mem_access.h"

namespace Vmm {

class Vcpu_ptr : public Generic_vcpu_ptr
{
  Arm::Hsr decode_mmio_slowpath() const;

public:
  explicit Vcpu_ptr(l4_vcpu_state_t *s) : Generic_vcpu_ptr(s) {}

  bool pf_write() const
  { return hsr().pf_write(); }

  static l4_uint32_t cntfrq()
  {
    l4_uint32_t x;
    asm volatile("mrc  p15, 0, %0, c14, c0, 0" : "=r" (x));
    return x;
  }

  static l4_uint64_t cntvct()
  {
    l4_uint64_t x;
    asm volatile ("mrrc p15, 1, %Q0, %R0, c14" : "=r"(x));
    return x;
  }

  static l4_uint64_t cntv_cval()
  {
    l4_uint64_t x;
    asm volatile ("mrrc p15, 3, %Q0, %R0, c14" : "=r"(x));
    return x;
  }

  void thread_attach()
  {
    control_ext(L4::Cap<L4::Thread>());
    reinterpret_cast<l4_utcb_t **>(l4_vcpu_e_info_user(_s))[0] = l4_utcb();
  }

  Arm::Hsr hsr() const
  { return Arm::Hsr(_s->r.err); }

  void jump_instruction() const
  { _s->r.ip += 2 << hsr().il(); }

  /**
   * Check whether register 'x' is a user mode register for the current mode
   *
   * \retval true   Register is a normal register accessible in l4_vcpu_state_t
   * \retval false  Register is a banked register which needs special treatment
   */
  bool use_ureg(unsigned x) const
  {
    // registers < 8 are always the user registers
    if (x < 8)
      return true;

    // one byte for each (legal) mode, where a set bit (x - 8) means
    // register r[x] is a user register, modes are
    //
    //   usr,  fiq,  irq,  svc,
    //      ,     ,  mon,  abt,
    //      ,     ,  hyp,  und,
    //      ,     ,     ,  sys
    //
    // fiq is handled separately, mon/hyp are invalid (trap to el2/el3).
    static l4_uint8_t const i[] =
      { 0xff, 0x00, 0x3f, 0x3f,
        0x00, 0x00, 0x00, 0x3f,
        0x00, 0x00, 0x00, 0x3f,
        0x00, 0x00, 0x00, 0xff };

    return i[_s->r.flags & 0x0f] & (1 << (x - 8));
  }

  /**
   * Caculate jump offset used for accessing non-user SP and LR in
   * 'irq', 'svc', 'abt' or 'und' mode
   *
   * The calculation does not check whether the mode is valid.
   *
   * \return  Jump offset
   */
  unsigned mode_offs() const
  {
    // mode (lower 5bits of flags):
    //
    //   0x12 -> 0, irq
    //   0x13 -> 2, svc
    //   0x17 -> 4, abt
    //   0x1b -> 6, und
    //
    // all other (non hyp) modes use all user registers, are handled
    // separately (fiq) or are illegal
    return ((_s->r.flags + 1) >> 1) & 0x6;
  }

  __attribute__((target("arm"))) l4_umword_t get_gpr(unsigned x) const;
  __attribute__((target("arm"))) void set_gpr(unsigned x, l4_umword_t value) const;

  l4_umword_t get_sp() const
  {
    return get_gpr(13);
  }

  l4_umword_t get_lr() const
  {
    return get_gpr(14);
  }

  Mem_access decode_mmio() const
  {
    Mem_access m;

    // might be an "extra load/store" instruction that is not decoded in HSR
    if (!hsr().pf_isv())
      _s->r.err = decode_mmio_slowpath().raw();

    if (!hsr().pf_isv() || hsr().pf_srt() > 14)
      {
        m.access = Mem_access::Other;
        return m;
      }

    m.width = hsr().pf_sas();
    m.access = hsr().pf_write() ? Mem_access::Store : Mem_access::Load;

    if (m.access == Mem_access::Store)
      {
	m.value = get_gpr(hsr().pf_srt());
	if (m.width == Mem_access::Wd64)
          m.value |= (l4_uint64_t)get_gpr(hsr().pf_uvmm_srt2()) << 32;
      }

    return m;
  }

  void writeback_mmio(Mem_access const &m) const
  {
    assert(m.access == Mem_access::Load);

    l4_uint64_t v = reg_extend_width(m.value, hsr().pf_sas(), hsr().pf_sse());
    set_gpr(hsr().pf_srt(), v);
    if (m.width == Mem_access::Wd64)
      set_gpr(hsr().pf_uvmm_srt2(), v >> 32);
  }

  Arm::Vtmr vtmr() const
  { return Arm::Vtmr(l4_vcpu_e_read_32(_s, L4_VCPU_E_VTMR_CFG)); }

  void vtmr(Arm::Vtmr cfg)
  { l4_vcpu_e_write_32(_s, L4_VCPU_E_VTMR_CFG, cfg.raw); }
};

} // namespace
