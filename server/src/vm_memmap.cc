/*
 * Copyright (C) 2018 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
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
