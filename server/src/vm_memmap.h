/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <l4/cxx/pair>

#include "mmio_device.h"
#include "mem_types.h"
#include "debug.h"

inline void *operator new (size_t, void *p) { return p; }

namespace Vmm {

class Vm_mem
{
  // Gic dist+redist
  enum { Capacity = 2, };

public:
  using Element = cxx::Pair<Region, Vmm::Mmio_device*>;

  Vm_mem();
  Vm_mem(Vm_mem const &other) = delete;

  void add_mmio_device(Region const &region, Vmm::Mmio_device *dev)
  {
    insert(region, dev);
  }

  void insert(Region const &region, Vmm::Mmio_device *device)
  {
    if (_size >= Capacity)
      Fatal().abort("Vm_mem overflow");

    new (get(_size++)) Element(region, device);
  }

  Element const *find(Region const &key) const;

  Element *begin() { return get(0); }
  Element *end() { return get(_size); }
  Element const *begin() const { return get(0); }
  Element const *end() const { return get(_size); }

private:
  Element *get(unsigned idx)
  {
    return reinterpret_cast<Element *>(&_arr[idx * sizeof(Element)]);
  }

  Element const *get(unsigned idx) const
  {
    return reinterpret_cast<Element const *>(&_arr[idx * sizeof(Element)]);
  }

  char _arr[sizeof(Element) * Capacity] __attribute__((aligned(__alignof(Element))));
  unsigned _size = 0;
};

} // namespace
