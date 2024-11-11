/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include <l4/re/env>
#include <l4/sys/debugger.h>
#include <l4/util/thread.h>
#include <l4/util/util.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "guest.h"
#include "irq_svr.h"
#include "utcb_alloc.h"
#include "vm_ctrl.h"

#ifdef CONFIG_BID_STATIC_HEAP
// provided by main_stat.ld linkmap file
extern char __heap_start[];
extern char __heap_end[];
#else
#define HEAP_ELEMENTS (CONFIG_TVMM_HEAP_SIZE / sizeof(unsigned long))
static unsigned long __heap[HEAP_ELEMENTS];
char * const __heap_start = reinterpret_cast<char *>(&__heap[0]);
char * const __heap_end = reinterpret_cast<char *>(&__heap[HEAP_ELEMENTS]);
#endif

static char *heap_pos = __heap_start;

void *malloc(size_t size)
{
  if (0)
    printf("malloc(%zu)\n", size);

  size = (size + 15U) & ~(size_t)15U;
  char *ret = heap_pos;
  heap_pos += size;

  if (heap_pos > __heap_end)
    Fatal().abort("OOM\n");

  return (void *)ret;
}

void free(void *ptr)
{ Err().printf("BUG: free(%p)\n", ptr); }

void *operator new(size_t sz) { return malloc(sz); }
void operator delete(void *m) { free(m); }
#if __cplusplus >= 201400
void operator delete(void *m, size_t) noexcept { free(m); }
#endif


extern "C" void L4_NORETURN __cxa_pure_virtual(void);
void __cxa_pure_virtual(void)
{
  Fatal().abort("__cxa_pure_virtual\n");
}

namespace {

L4UTIL_THREAD_FUNC(vm_thread)
{
  l4_cap_idx_t main_thread = L4Re::Env::env()->main_thread().cap();

  // Receive vmm and cpu parameters
  if (l4_ipc_error(l4_ipc_receive(main_thread, l4_utcb(), L4_IPC_NEVER),
                   l4_utcb()))
    Fatal().abort("vm_thread l4_ipc_receive#1 failed\n");

  auto *vmm = (Vmm::Guest *)l4_utcb_mr()->mr[0];
  auto *cpu = (Vmm::Cpu_dev *)l4_utcb_mr()->mr[1];

  // Sync with main thread to call start_vm_thread()
  if (l4_ipc_error(l4_ipc_receive(main_thread, l4_utcb(), L4_IPC_NEVER),
                   l4_utcb()))
    Fatal().abort("vm_thread l4_ipc_receive#2 failed\n");

  vmm->run(cpu);

  Err().printf("ERROR: we must never reach this....\n");
  l4_sleep_forever();
}

void spawn_vm_thread(L4::Cap<L4::Thread> thread, Vmm::Guest *vmm, Vmm::Cpu_dev *cpu)
{
  enum { Stack_size = 2048 };

  void *stack = (char *)malloc(Stack_size) + Stack_size;
  auto *env = L4Re::Env::env();

  L4::Thread::Attr attr;
  attr.pager(env->rm());
  attr.exc_handler(env->rm());
  attr.bind((l4_utcb_t *)alloc_utcb(L4_UTCB_OFFSET), env->task());
  if (l4_error(thread->control(attr)) < 0)
    Fatal().abort("thread control failed\n");

  if (l4_error(thread->ex_regs((l4_umword_t)&vm_thread, (l4_umword_t)stack,
                               L4_THREAD_EX_REGS_ARM_SET_EL_EL1)) < 0)
    Fatal().abort("create thread\n");

  // Forward cpu and vmm arguments to vcpu thread. This synchronizes the main
  // thread so that IPC gates can be safely bound because the thread is not
  // dead anymore.
  l4_utcb_mr()->mr[0] = (l4_umword_t)vmm;
  l4_utcb_mr()->mr[1] = (l4_umword_t)cpu;
  if (l4_ipc_error(l4_ipc_send(thread.cap(), l4_utcb(), l4_msgtag(0, 2, 0, 0),
                               L4_IPC_NEVER),
                   l4_utcb()))
    Fatal().abort("spawn l4_ipc_send failed\n");
}

void start_vm_thread(L4::Cap<L4::Thread> thread)
{
  if (l4_ipc_error(l4_ipc_send(thread.cap(), l4_utcb(), l4_msgtag(0, 0, 0, 0),
                               L4_IPC_NEVER),
                   l4_utcb()))
    Fatal().abort("start l4_ipc_send failed\n");
}

size_t malloc_pool_avail()
{ return __heap_end - heap_pos; }

size_t malloc_pool_size()
{ return __heap_end - __heap_start; }

Dbg info(Dbg::Core, Dbg::Info);

}

int main(int, char *argv[])
{
  char **args = &argv[1];
  unsigned num_vms = 0;
  Vmm::Cpu_dev *vms[CONFIG_TVMM_MAX_VMS] = {0};

  Vmm::Guest *vmm = 0, *vmm0 = 0;
  Vmm::Cpu_dev *cpu = 0, *cpu0 = 0;
  auto *env = L4Re::Env::env();

  while (char *arg = *args++)
    {
      // First VM independent options.
      switch (arg[0])
        {
          case 'D':
            if (Dbg::set_verbosity(&arg[1]) < 0)
              {
                Err().printf("Invalid verbosity: %s\n", &arg[1]);
                return 1;
              }
            continue;

          // "V<task>[:<thread>]" - New VM with vCPU thread (or implicit main thread)
          case 'V':
          {
            if (num_vms >= CONFIG_TVMM_MAX_VMS)
              {
                Err().printf("Maximum number of VMs reached!\n");
                return 1;
              }

            char *task_str = &arg[1];
            char *thread_str = strchr(task_str, ':');
            if (thread_str)
              *thread_str++ = '\0';

            L4::Cap<L4::Vm> vm_task = env->get_cap<L4::Vm>(task_str);
            if (!vm_task.is_valid())
              {
                Err().printf("VM task '%s' cap not valid!\n", task_str);
                return 1;
              }

            if (!thread_str && vmm0)
              {
                Err().printf("Main thread alread used by another VM!\n");
                return 1;
              }

            L4::Cap<L4::Thread> vm_thread
              = thread_str ? env->get_cap<L4::Thread>(thread_str)
                           : env->main_thread();


            info = Dbg(Dbg::Core, Dbg::Info, task_str);
            info.printf("Spawn VM '%s', thread %lx\n", task_str, vm_thread.cap());

#ifndef NDEBUG
            l4_debugger_set_object_name(vm_task.cap(), task_str);
            l4_debugger_set_object_name(vm_thread.cap(), task_str);
#endif

            vmm = new Vmm::Guest(vm_task, task_str);
            cpu = new Vmm::Cpu_dev(vm_thread, vmm);

            if (!thread_str)
              {
                vmm0 = vmm;
                cpu0 = cpu;
              }
            else
              {
                vms[num_vms++] = cpu;
                spawn_vm_thread(cpu->thread_cap(), vmm, cpu);
              }

            continue;
          }

          default:
            break;
        }

      // All other options need a valid VM
      if (!vmm || !cpu)
        {
          Err().printf("Need to define a VM first!\n");
          return 1;
        }

      switch (arg[0])
        {
          // "I<cap>:<num>" - Irq pass through from <cap> to virtual irq <num>
          case 'I':
          {
            char *name = &arg[1];
            char *n = strchr(name, ':');
            *n++ = '\0';
            unsigned num = strtoul(n, NULL, 0);
            info.printf("Attach Irq '%s' to %u\n", name, num);
            L4::Cap<L4::Irq> irq = L4Re::Env::env()->get_cap<L4::Irq>(name);
            new Vdev::Irq_svr(cpu, irq, vmm->gic(), num);
            break;
          }

          // "E<entry>" - VM entry point
          case 'E':
          {
            unsigned long entry = strtoul(&arg[1], NULL, 0);
            if (entry == 0 || entry == ULONG_MAX)
              {
                Err().printf("Invalid entry point: %s\n", &arg[1]);
                return 1;
              }

            cpu->prepare_vcpu_startup(entry);
            break;
          }

          // "P<min>:<max>" - Irq priority range mapping (Fiasco priorities)
          case 'P':
          {
            char *from_str = &arg[1];
            char *to_str = strchr(from_str, ':');
            *to_str++ = '\0';
            unsigned from = strtoul(from_str, NULL, 0);
            unsigned to = strtoul(to_str, NULL, 0);
            info.printf("Irq priority range [%u..%u]\n", from, to);
            vmm->set_irq_priority_range(from, to);
            break;
          }

#ifdef CONFIG_TVMM_VM_CTRL_IFC
          // "C<ctrl-cap>" - Add Tvmm::Ctrl IPC control interface
          case 'C':
          {
            char *cap_name = &arg[1];
            auto *ctrl = new Vm_ctrl(vmm);
            if (!cpu->registry()->register_obj(ctrl, cap_name))
              {
                Err().printf("Invalid control cap: %s\n", cap_name);
                return 1;
              }
            break;
          }
#endif

#ifdef CONFIG_TVMM_ELF_LOADER
          // "L<addr>" - Load elf file from address
          case 'L':
          {
            char *elf_addr_str = &arg[1];
            unsigned long elf_addr = strtoul(elf_addr_str, NULL, 0);
            info.printf("Load elf from 0x%lx\n", elf_addr);
            vmm->load_elf(elf_addr, cpu);
            break;
          }
#endif

          default:
            Err().printf("Invalid argument: %s\n", arg);
            return 1;
        }
    }

  // Start VMs that have their own thread
  for (unsigned i = 0; i < num_vms; i++)
    {
      Vmm::Cpu_dev *cpu = vms[i];
      start_vm_thread(cpu->thread_cap());
    }

  Dbg(Dbg::Core, Dbg::Info)
    .printf("Heap: %zu/%zu bytes free.\n", malloc_pool_avail(),
            malloc_pool_size());

  // Signal that we're ready
  auto parent = L4Re::Env::env()->parent();
  if (parent.is_valid())
    parent->signal(1, 0);

  // The first VM with cpu0 is re-using the main thread...
  if (vmm0)
    {
      vmm0->run(cpu0);
      Err().printf("ERROR: we must never reach this....\n");
    }

  l4_sleep_forever();
  return 0;
}
