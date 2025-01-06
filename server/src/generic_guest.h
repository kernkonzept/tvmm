/*
 * Copyright (C) 2015, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <l4/sys/vm>
#include <l4/util/util.h>

#include "consts.h"
#include "debug.h"
#include "generic_cpu_dev.h"
#include "mem_types.h"
#include "vm_memmap.h"

namespace Vmm {

class Generic_guest
{
public:
  enum State
  {
    Running,
    Stopped,
    Shutdown,
    Crashed
  };

  struct State_listener
  {
    virtual void state_change() = 0;
  };

public:
  Generic_guest(L4::Cap<L4::Vm> task, char const *name)
  : _task(task), _name(name)
  {}
  virtual ~Generic_guest();

  void halt_vm(Vcpu_ptr /*current_vcpu*/)
  {
    Err().printf("%s: VM entered a fatal state. Halting.\n", _name);
    state(Crashed);
  }

  int handle_mmio(l4_addr_t pfa, Vcpu_ptr vcpu);

  void add_mmio_device(Region const &region, Vmm::Mmio_device *dev)
  {
    info().printf("Virtual MMIO device %p @ [0x%lx..0x%lx]\n", dev,
		  region.start.get(), region.end.get());
    _memmap.add_mmio_device(region, dev);
  }

  L4::Cap<L4::Vm> vm_task()
  { return _task; }

  char const *name()
  { return _name; }

  State state() const
  { return _state; }

  void set_state_listener(State_listener *listener)
  { _state_listener = listener; }

protected:
  Dbg warn()
  { return Dbg(Dbg::Core, Dbg::Warn, _name); }

  Dbg info()
  { return Dbg(Dbg::Core, Dbg::Info, _name); }

  Dbg trace()
  { return Dbg(Dbg::Core, Dbg::Trace, _name); }

  virtual bool inject_abort(l4_addr_t /*pfa*/, Vcpu_ptr /*vcpu*/)
  { return false; }

  void state(State new_state)
  {
    if (_state_listener && _state != new_state)
      _state_listener->state_change();
    _state = new_state;
  }

  Vm_mem _memmap;
  L4::Cap<L4::Vm> _task;
  char const *_name;
  State _state = Running;
  State_listener *_state_listener = nullptr;
};

} // namespace
