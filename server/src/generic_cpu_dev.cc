/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include <l4/re/env>
#include <l4/re/util/br_manager>
#include <l4/re/util/object_registry>
#include <l4/sys/vcpu.h>
#include <stdio.h>

#include "generic_cpu_dev.h"
#include "utcb_alloc.h"
#include "debug.h"

namespace Vmm {

Vcpu_ptr Generic_cpu_dev::alloc_vcpu()
{
  l4_addr_t vcpu_addr = alloc_utcb(L4_VCPU_STATE_EXT_SIZE);

  Dbg(Dbg::Cpu, Dbg::Info).printf("Created VCPU @ %lx\n", vcpu_addr);

  return Vcpu_ptr((l4_vcpu_state_t *)vcpu_addr);
}

Generic_cpu_dev::Generic_cpu_dev(L4::Cap<L4::Thread> thread, Guest *vmm)
: _vcpu(alloc_vcpu()), _thread(thread),
  _registry(&_bm, thread, L4Re::Env::env()->factory())
{
  _vcpu.set_vmm(vmm);
  _vcpu.set_ipc_registry(&_registry);
  _vcpu.set_ipc_bm(&_bm);
}

void
Generic_cpu_dev::startup()
{
  _vcpu.prepare_ipc_wait(l4_utcb());
  _vcpu.thread_attach();

  // Dispatch any IPC that was pending before we enabled vCPU mode.
  while (_vcpu.wait_for_ipc(l4_utcb(), L4_IPC_BOTH_TIMEOUT_0));
}

}
