/*
 * Copyright (C) 2018, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include "debug.h"
#include "vm_memmap.h"

Vmm::Vm_mem::Vm_mem()
{}

Vmm::Vm_mem::Element const *
Vmm::Vm_mem::find(Vmm::Region const &key) const
{
  for (auto &i : *this)
    {
      if (i.first.contains(key))
        return &i;
    }

  return end();
}
