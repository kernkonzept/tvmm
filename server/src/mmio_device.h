/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/sys/vm>
#include <l4/sys/l4int.h>
#include <l4/sys/types.h>

#include "vcpu_ptr.h"
#include "mem_access.h"
#include "mem_types.h"
#include "consts.h"

namespace Vmm {

/**
 * Interface for any device that processes access to special guest-physical
 * memory regions.
 */
struct Mmio_device
{
  virtual ~Mmio_device() = default;

  /**
   * Callback on memory access.
   *
   * \param pfa      Guest-physical address where the access occurred.
   * \param offset   Accessed address relative to the beginning of the
   *                 device's memory region.
   * \param vcpu     Virtual CPU from which the memory was accessed.
   * \param vm_task  Capability to the guest memory.
   * \param s        Guest-physical address of start of device memory region.
   * \param e        Guest-physical address of end of device memory region.
   *
   * \retval < 0         if memory access was faulty, the error code.
   * \retval Retry       if memory was mapped and access can be retried.
   * \retval Jump_instr  if memory access could be handled.
   * \
   */
  virtual int access(l4_addr_t pfa, l4_addr_t offset, Vcpu_ptr vcpu,
                     L4::Cap<L4::Vm> vm_task, l4_addr_t s, l4_addr_t e) = 0;

protected:
  static Mem_access decode(l4_addr_t pfa, l4_addr_t offset, Vcpu_ptr vcpu);
};

/**
 * Mixin for devices that trap read and write access to physical guest memory.
 *
 * The base class DEV needs to provide two functions read() and write() that
 * implement the actual functionality behind the memory access. Those
 * functions must be defined as follows:
 *
 *     l4_umword_t read(unsigned reg, char size, unsigned cpu_id);
 *
 *     void write(unsigned reg, char size, l4_umword_t value, unsigned cpu_id);
 *
 * `reg` is the address offset into the devices memory region. `size`
 * describes the width of the access (see Vmm::Mem_access::Width) and
 * `cpu_id` the accessing CPU (currently unused).
 */
template<typename DEV>
struct Mmio_device_t : Mmio_device
{
  int access(l4_addr_t pfa, l4_addr_t offset, Vcpu_ptr vcpu,
             L4::Cap<L4::Vm>, l4_addr_t, l4_addr_t) override
  {
    auto insn = decode(pfa, offset, vcpu);

    if (insn.access == Vmm::Mem_access::Other)
      return -L4_ENXIO;

    if (insn.access == Vmm::Mem_access::Store)
      dev()->write(offset, insn.width, insn.value);
    else
      {
        insn.value = dev()->read(offset, insn.width);
        vcpu.writeback_mmio(insn);
      }

    return Jump_instr;
  }

private:
  DEV *dev()
  { return static_cast<DEV *>(this); }
};

} // namespace
