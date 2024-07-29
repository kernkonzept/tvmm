/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017-2021, 2023-2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *            Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */

#pragma once

#include <debug.h>
#include <vcpu_ptr.h>
#include <l4/re/util/br_manager>
#include <l4/re/util/object_registry>

namespace Vmm {

class Guest;

class Generic_cpu_dev
{
private:
  static Vcpu_ptr alloc_vcpu();

public:
  Generic_cpu_dev(L4::Cap<L4::Thread> thread, Guest *vmm);

  Vcpu_ptr vcpu() const
  { return _vcpu; }

  virtual void reset() = 0;

  virtual void startup();

  virtual void start() = 0;

  L4::Cap<L4::Thread> thread_cap() const
  { return _thread; }

  L4Re::Util::Object_registry *registry()
  { return &_registry; }

protected:
  Vcpu_ptr _vcpu;
  L4::Cap<L4::Thread> _thread;
  L4Re::Util::Br_manager _bm;
  L4Re::Util::Object_registry _registry;
};


}
