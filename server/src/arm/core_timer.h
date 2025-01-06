/*
 * Copyright (C) 2016, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include "irq.h"
#include "vcpu_ptr.h"

namespace Vdev {

struct Core_timer : public Gic::Virq_handler
{
  Core_timer(char const *vm_name)
  {
    unsigned int cntfrq = Vmm::Vcpu_ptr::cntfrq();

    init_large_scale(cntfrq);
    init_normal_scale(cntfrq);

    Dbg(Dbg::Cpu, Dbg::Info, vm_name)
      .printf("Guest timer frequency is %d\n"
              "using (%d/%d), (%d/%d) to calculate timeouts\n",
              cntfrq, _scale, _scaled_ticks_per_us, _cyc2ms_scale, _shift);
  }

  void set_vcpu(Vmm::Vcpu_ptr vcpu)
  { _vcpu = vcpu; }

  /*
   * We use "scaling math" as described in a comment in
   * linux/arch/x86/kernel/tsc.c. We use micro seconds instead of nano
   * seconds and adjust the math accordingly.
   *
   * convert from cycles(32bits) => microseconds (32Bit), ms 64Bit
   *   basic equation:
   *              ms = cycles / (freq / ms_per_sec)
   *              ms = cycles * (ms_per_sec / freq)
   *              ms = cycles * (10^6 / (timer_khz * 10^3))
   *              ms = cycles * (10^3 / timer_khz)
   *
   *      Then we use scaling math (suggested by george@mvista.com) to get:
   *              ms = cycles * (10^3 * 2^SC / timer_khz) / 2^SC
   *
   *              cyc2ms_scale = (10^3 * 2^SC / timer_khz)
   *              ms = cycles * cyc2ms_scale / 2^SC
   *
   *
   * We select SC so that cyc2ms_scale is the largest scaling factor
   * fitting into a 32Bit und use a simple shift for the division by
   * 2^SC.
   */

  /**
   * Convert timer ticks into micro seconds
   *
   * \param ticks Number of timer ticks
   * \return Number of microseconds
   */
  l4_uint64_t get_micro_seconds(l4_uint64_t ticks)
  {
    if (L4_LIKELY((ticks >> 32) == 0))
      {
        /*
         * With a time tick rate of 1GHz this will cover up to 49
         * days. On Arm we have timer rates specified in MHz, so this
         * will last even longer.
         *
         * The calculation rounds down and delivers a timeout that
         * triggers up to 1 microsecond early.
         */
        l4_uint64_t tmp = (l4_uint32_t)ticks;
        return ((tmp * _cyc2ms_scale) >> _shift);
      }

    /*
     * We divide first to prevent overflows. This will make it a little bit less
     * precise, but this path should not be taken anyway.
     */
    return (ticks / _scaled_ticks_per_us) * _scale;
  }

  /**
   * Calculate constants used to convert timer ticks (> 2^32) into
   * micro seconds. Sets member variables _scaled_ticks_per_us and
   * _scale. Assumes a timer rate >= 1Mhz
   *
   * \param freq Rate of the timer (e.g. 12.5Mhz = 12500000)
   */
  void init_large_scale(l4_uint32_t freq)
  {
    // Scale factors for numbers >= 2^32
    _scale = 1000000; // micro seconds per second
    _scaled_ticks_per_us = freq;
    assert(_scale <= _scaled_ticks_per_us);

    while ((_scale > 1) && (_scaled_ticks_per_us % 10 == 0))
      {
        _scale /= 10;
        _scaled_ticks_per_us /= 10;
      }
    if ((_scale > 1) && (_scaled_ticks_per_us % 10 == 5))
      {
        _scale /= 5;
        _scaled_ticks_per_us /= 5;
      }
  }

  /**
   * Calculate constants used to convert timer ticks (< 2^32) into
   * micro seconds. Sets member variables _cyc2ms_scale and
   * _shift. Assumes a timer rate >= 1Khz
   *
   * \param freq Rate of the timer (e.g. 12.5Mhz = 12500000)
   */
  void init_normal_scale(l4_uint32_t freq)
  {
    assert(freq >= 1000);
    l4_uint32_t timer_khz = freq / 1000;
    for (int i = 12; i < 30; ++i)
      {
        l4_uint64_t scale = (1000ULL << i) / timer_khz;
        if (scale >= (1ULL << 32))
          return;

        _cyc2ms_scale = (l4_uint32_t)scale;
        _shift = i;
      }
  }

  void eoi() override
  {}

  void set_priority(unsigned prio) override
  {
#ifdef IRQ_PRIORITIES_NOT_IMPLEMENTED_YET
    Vmm::Arm::Vtmr v = _vcpu.vtmr();
    v.host_prio() = prio;
    _vcpu.vtmr(v);
#else
    static_cast<void>(prio);
#endif
  }

  void configure(l4_umword_t cfg) override
  {
    Vmm::Arm::Gic_h::Vcpu_irq_cfg c(cfg);

    Vmm::Arm::Vtmr v = _vcpu.vtmr();
    v.grp1() = c.grp1();
    v.vgic_prio() = c.prio();
    _vcpu.vtmr(v);
  }

  void enable() override
  { Vmm::Arm::Vtmr v = _vcpu.vtmr(); v.enabled() = 1; _vcpu.vtmr(v); }

  void disable() override
  { Vmm::Arm::Vtmr v = _vcpu.vtmr(); v.enabled() = 0; _vcpu.vtmr(v); }

  void clear_pending() override
  { Vmm::Arm::Vtmr v = _vcpu.vtmr(); v.pending() = 0; _vcpu.vtmr(v); }

  void set_pending() override
  { Vmm::Arm::Vtmr v = _vcpu.vtmr(); v.pending() = 1; _vcpu.vtmr(v); }

private:
  l4_uint32_t _scale, _scaled_ticks_per_us;
  l4_uint32_t _cyc2ms_scale, _shift;
  Vmm::Vcpu_ptr _vcpu{0};
};

}
