/*
 * Copyright (C) 2015-2021, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "vcpu_ptr.h"

namespace Vmm {

Arm::Hsr
Vcpu_ptr::decode_mmio_slowpath() const
{
  Arm::Hsr h = hsr();

  if ((_s->r.flags & (1U << 5)) == 0)
    {
      // A32 instruction
      l4_uint32_t opcode = *(l4_uint32_t*)_s->r.ip;

      // Extra load/store? A regular LDR/STR is decoded by HW in HSR
      // automatically.
      if ((opcode & 0x0e000090U) != 0x90U)
        return h;

      // reject wback case
      if ((opcode & (1U << 24)) == 0 || (opcode & (1U << 21)) != 0)
        return h;

      unsigned t = (opcode >> 12) & 0xfU;
      switch (opcode & 0x100060U)
        {
        case 0x40: // LDRD
          assert(!h.pf_write());
          h.pf_isv() = 1;
          h.pf_sas() = 3; // Doubleword
          h.pf_srt() = t;
          h.pf_uvmm_srt2() = t + 1U;
          break;

        case 0x60: // STRD
          assert(h.pf_write());
          h.pf_isv() = 1;
          h.pf_sas() = 3; // Doubleword
          h.pf_srt() = t;
          h.pf_uvmm_srt2() = t + 1U;
          break;

        default:
          // All other extra load/store instructions should be decoded by HW.
          // If we end up here they use PC as source/destination. This is not
          // supported by uvmm for LDR/STR either.
          break;
        }
    }
  else
    {
      // Thumb instruction...
      l4_uint16_t opc1 = *(l4_uint16_t*)_s->r.ip;

      // Load/store dual, load/store exclusive, load-acquire/store-release,
      // and table branch group?
      if ((opc1 & 0xfe40U) != 0xe840U)
        return h;

      // Load/store dual?
      if ((opc1 & 0x0120U) == 0U)
        return h;

      // reject wback case
      if (opc1 & (1U << 5))
        return h;

      l4_uint16_t opc2 = *(l4_uint16_t*)(_s->r.ip + 2U);
      h.pf_isv() = 1;
      h.pf_sas() = 3;
      h.pf_srt() = opc2 >> 12;
      h.pf_uvmm_srt2() = (opc2 >> 8) & 0xfU;
    }

  return h;
}

l4_umword_t Vcpu_ptr::get_gpr(unsigned x) const
{
  if (L4_UNLIKELY(x > 14))
    return 0;

  if (use_ureg(x))
    switch (x)
      {
      case 14: return _s->r.lr;
      case 13: return _s->r.sp;
      default: return _s->r.r[x];
      }

  if (0)
    printf("SPECIAL GET GPR: m=%2lx x=%u\n", (_s->r.flags & 0x1f), x);

  l4_umword_t res;
  if ((_s->r.flags & 0x1f) == 0x11) // FIQ
    {
      // assembly implementation of
      // switch(x - 8) {
      //    case n:
      //        read banked fiq register (n + 8)
      //        break;
      asm (".arch_extension virt\n"
           "add pc, pc, %[r]\n"
           "nop\n"
           "mrs %[res], R8_fiq \n b 2f\n"
           "mrs %[res], R9_fiq \n b 2f\n"
           "mrs %[res], R10_fiq\n b 2f\n"
           "mrs %[res], R11_fiq\n b 2f\n"
           "mrs %[res], R12_fiq\n b 2f\n"
           "mrs %[res], SP_fiq \n b 2f\n"
           "mrs %[res], LR_fiq \n"
           "2:\n" : [res]"=r"(res) : [r]"r"((x - 8) * 8));
      return res;
    }

  // Should we check whether we have a valid mode (irq, svc, abt, und) here?
  //
  // assembly implementation of
  // switch(f(mode, x-13)) {
  //    case x:
  //        read banked lr/sp register for mode
  //        break;
  asm (".arch_extension virt\n"
       "add pc, pc, %[r]\n"
       "nop\n"
       "mrs %[res], SP_irq \n b 2f\n"
       "mrs %[res], LR_irq \n b 2f\n"
       "mrs %[res], SP_svc\n b 2f\n"
       "mrs %[res], LR_svc\n b 2f\n"
       "mrs %[res], SP_abt\n b 2f\n"
       "mrs %[res], LR_abt \n b 2f\n"
       "mrs %[res], SP_und \n b 2f\n"
       "mrs %[res], LR_und \n"
       "2:\n" : [res]"=r"(res) : [r]"r"((x - 13 + mode_offs()) * 8));
  return res;
}

void Vcpu_ptr::set_gpr(unsigned x, l4_umword_t value) const
{
  if (L4_UNLIKELY(x > 14))
    return;

  if (use_ureg(x))
    switch (x)
      {
      case 14: _s->r.lr = value; return;
      case 13: _s->r.sp = value; return;
      default: _s->r.r[x] = value; return;
      }

  if (0)
    printf("SPECIAL SET GPR: m=%2lx x=%u\n", (_s->r.flags & 0x1f), x);

  if ((_s->r.flags & 0x1f) == 0x11) // FIQ
    {
      // assembly implementation of
      // switch(x - 8) {
      //    case n:
      //        write banked fiq register (n + 8)
      //        break;
      asm ("add pc, pc, %[r]\n"
           "nop\n"
           "msr R8_fiq,  %[v] \n b 2f\n"
           "msr R9_fiq,  %[v] \n b 2f\n"
           "msr R10_fiq, %[v] \n b 2f\n"
           "msr R11_fiq, %[v] \n b 2f\n"
           "msr R12_fiq, %[v] \n b 2f\n"
           "msr SP_fiq,  %[v] \n b 2f\n"
           "msr LR_fiq,  %[v] \n"
           "2:\n" : : [v]"r"(value), [r]"r"((x - 8) * 8));
      return;
    }

  // Should we check whether we have a valid mode (irq, svc, abt, und) here?
  //
  // assembly implementation of
  // switch(f(mode, x-13)) {
  //    case x:
  //        write banked lr/sp register for mode
  //        break;
  asm ("add pc, pc, %[r]\n"
       "nop\n"
       "msr SP_irq, %[v] \n b 2f\n"
       "msr LR_irq, %[v] \n b 2f\n"
       "msr SP_svc, %[v] \n b 2f\n"
       "msr LR_svc, %[v] \n b 2f\n"
       "msr SP_abt, %[v] \n b 2f\n"
       "msr LR_abt, %[v] \n b 2f\n"
       "msr SP_und, %[v] \n b 2f\n"
       "msr LR_und, %[v] \n"
       "2:\n" : : [v]"r"(value), [r]"r"((x - 13 + mode_offs()) * 8));
}

}
