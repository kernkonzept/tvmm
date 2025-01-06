/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2015-2022, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Alexander Warg <alexander.warg@kernkonzept.com>
 *            Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */

#include <l4/sys/cache.h>

#include "guest.h"
#include "guest_subarch.h"
#include "irq.h"
#include "sys_reg.h"
#include "vm_print.h"

namespace Vmm {

namespace {

using namespace Arm;

struct DCCSR : Sys_reg_ro
{
  l4_uint32_t flip = 0;

  l4_uint64_t read(Vmm::Vcpu_ptr, Key) override
  {
    // printascii in Linux is doing busyuart which wants to see a
    // busy flag to quit its loop while waituart does not want to
    // see a busy flag; this little trick makes it work
    flip ^= 1 << 29;
    return flip;
  }
};

struct DBGDTRxX : Sys_reg_const<0>
{
  void write(Vmm::Vcpu_ptr, Key, l4_uint64_t v) override
  {
    putchar(v);
  }
};

// Helper for logging read/write accesses to groups of known system registers
// where the 'n' value is encoded by the 'CRm'.
// Write accesses are not performed. Read accesses return 0.
struct Sys_reg_log_n : Sys_reg
{
  Sys_reg_log_n(char const *name)
  : name(name)
  {}

  void write(Vmm::Vcpu_ptr vcpu, Key k, l4_uint64_t v) override
  {
    Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
      .printf("%08lx: msr %s%d_EL1 = %08llx (ignored)\n",
              vcpu->r.ip, name, (unsigned)k.crm(), v);
  }

  l4_uint64_t read(Vmm::Vcpu_ptr vcpu, Key k) override
  {
    Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
      .printf("%08lx: mrs %s%d_EL1 (read 0)\n",
              vcpu->r.ip, name, (unsigned)k.crm());
    return 0;
  }

  char const *name;
};

// Helper for logging read/write accesses to dedicated known system registers.
// Write accesses are not performed. Read accesses return 0.
struct Sys_reg_log : Sys_reg
{
  Sys_reg_log(char const *name)
  : name(name)
  {}

  void write(Vmm::Vcpu_ptr vcpu, Key, l4_uint64_t v) override
  {
    Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
      .printf("%08lx: msr %s = %08llx (ignored)\n", vcpu->r.ip, name, v);
  }

  l4_uint64_t read(Vmm::Vcpu_ptr vcpu, Key) override
  {
    Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
      .printf("%08lx: mrs %s (read 0)\n", vcpu->r.ip, name);
    return 0;
  }

  char const *name;
};


Vm_print_device vm_print;
Sys_reg_const<0> sys_reg_const_0;
DBGDTRxX dcc;

}

Guest::Guest(L4::Cap<L4::Vm> task, char const *name)
: Generic_guest(task, name), _timer(name)
{
  register_vm_handler(Hvc, &vm_print);
  _gic.setup_gic(this);

  Sys_reg *r = new DCCSR();
  add_sys_reg_aarch32(14, 0, 0, 1, 0, r); // DBGDSCRint
  add_sys_reg_aarch64( 2, 3, 0, 1, 0, r);
  // MDSCR_EL1 (we can map this to DBGSCRint as long as we only implement bit 29..30
  add_sys_reg_aarch64( 2, 0, 0, 2, 2, r);

  // DBGIDR
  add_sys_reg_aarch32(14, 0, 0, 0, 0, &sys_reg_const_0);

  add_sys_reg_aarch32(14, 0, 0, 5, 0, &dcc);
  add_sys_reg_aarch64( 2, 3, 0, 5, 0, &dcc);
}

void
Guest::run(Cpu_dev *cpu)
{
  cpu->startup();
  _cpu = cpu;

  auto vcpu = cpu->vcpu();
  vcpu->user_task = _task.cap();
  _gic.setup_cpu(cpu);
  _timer.set_vcpu(vcpu);

  // Setup timer for direct guest injection. Unless the kernel does not support
  // this feature we should not get a vtmr ppi (which is unsupported by us).
  Vmm::Arm::Vtmr cfg(0);
  cfg.vid() = 27;
  cfg.host_prio() = 0xff;
  cfg.direct() = 1;
  vcpu.vtmr(cfg);
  _gic.bind_cpulocal_virq_handler(27, &_timer);

  cpu->start();
}

l4_msgtag_t
Guest::handle_entry(Vcpu_ptr vcpu)
{
  auto *utcb = l4_utcb();

  while (state() != Running)
    vcpu.wait_for_ipc(utcb, L4_IPC_NEVER);

  vcpu.process_pending_ipc(utcb);
  _gic.schedule_irqs();

  L4::Cap<L4::Thread> myself;
  return myself->vcpu_resume_start(utcb);
}

static void dispatch_vm_call(Vcpu_ptr vcpu)
{
  vcpu.get_vmm()->handle_smccc_call(vcpu, Guest::Hvc);
}

static void dispatch_smc(Vcpu_ptr vcpu)
{
  vcpu.get_vmm()->handle_smccc_call(vcpu, Guest::Smc);
}

static void
guest_unknown_fault(Vcpu_ptr vcpu)
{
  auto *guest = vcpu.get_vmm();

  Err().printf("%s: unknown trap: err=%lx ec=0x%x ip=%lx lr=%lx\n",
	       guest->name(), vcpu->r.err, (int)vcpu.hsr().ec(), vcpu->r.ip,
	       vcpu.get_lr());
  guest->halt_vm(vcpu);
}

static void
guest_memory_fault(Vcpu_ptr vcpu)
{
  auto *guest = vcpu.get_vmm();

  switch (guest->handle_mmio(vcpu->r.pfa, vcpu))
    {
    case Retry: break;
    case Jump_instr: vcpu.jump_instruction(); break;
    default:
      Err().printf("%s: cannot handle VM memory access @ %lx ip=%lx lr=%lx\n",
		   guest->name(), vcpu->r.pfa, vcpu->r.ip, vcpu.get_lr());
      guest->halt_vm(vcpu);
      break;
    }
}

void
Vmm::Guest::suspend()
{
  info().printf("%s: suspend\n", name());
  if (_state == Running)
    state(Stopped);
}

void
Vmm::Guest::resume()
{
  info().printf("%s: resume\n", name());
  if (_state == Stopped)
    state(Running);
}

void
Vmm::Guest::reset()
{
  info().printf("%s: reset\n", name());
  _load_elf();
  _gic.reinit();
  _cpu->reset();
  l4_cache_dma_coherent_full();
  if (_state == Shutdown || _state == Crashed)
    state(Stopped);
}

void
Vmm::Guest::load_elf(unsigned long elf_addr, Cpu_dev *cpu)
{
  Loader::Elf_binary elf((void const *)elf_addr);
  if (!elf.is_valid())
    Fatal().abort("Invalid elf file\n");

  _elf = elf;
  cpu->prepare_vcpu_startup(_load_elf());
}

unsigned long
Vmm::Guest::_load_elf()
{
#ifdef CONFIG_TVMM_ELF_LOADER
  if (!_elf.is_valid())
    return 0;

  _elf.iterate_phdr([](Loader::Elf_phdr ph, void const *data)
    {
      if (ph.type() != PT_LOAD)
        return;

      l4_addr_t dest = ph.paddr();
      l4_addr_t size = ph.memsz();
      l4_addr_t off = 0;
      if (!size)
        return;

#ifdef CONFIG_PLATFORM_TYPE_s32z
      // accomodate for the AXIF read-only mirroring
      if (dest > 0x70000000)
        off = 0xb8800000U;
#endif

      memcpy((void *)(dest + off), (char const *)data + ph.offset(), ph.filesz());
      memset((void *)(dest + off + ph.filesz()), 0, size - ph.filesz());
      l4_cache_coherent(dest, dest + size);
      if (off)
        l4_cache_coherent(dest + off, dest + off + size);
    });

  return _elf.entry();
#else
  return 0;
#endif
}

bool
Vmm::Guest::inject_abort(l4_addr_t addr, Vcpu_ptr vcpu)
{
  // Inject an instruction abort?
  bool inst = vcpu.hsr().ec() == Hsr::Ec_iabt_low;
  return inject_abort(vcpu, inst, addr);
}

void
Vmm::Guest::wait_for_timer_or_irq(Vcpu_ptr vcpu)
{
  if (_gic.schedule_irqs())
    return;

  l4_timeout_t to = L4_IPC_NEVER;

  auto *utcb = l4_utcb();
  if ((l4_vcpu_e_read_32(*vcpu, L4_VCPU_E_CNTVCTL) & 3) == 1) // timer enabled and not masked
    {
      // calculate the timeout based on the VTIMER values !
      auto cnt = vcpu.cntvct();
      auto cmp = vcpu.cntv_cval();

      if (cmp <= cnt)
        return;

      l4_uint64_t diff = _timer.get_micro_seconds(cmp - cnt);
      if (0)
        printf("diff=%lld\n", diff);
      l4_rcv_timeout(l4_timeout_abs_u(l4_kip_clock(l4re_kip()) + diff, 8, utcb), &to);
    }

  vcpu.wait_for_ipc(utcb, to);
}

void
Vmm::Guest::handle_wfx(Vcpu_ptr vcpu)
{
  vcpu->r.ip += 2 << vcpu.hsr().il();
  if (vcpu.hsr().wfe_trapped()) // WFE
    return;

  wait_for_timer_or_irq(vcpu);
}

static void
guest_wfx(Vcpu_ptr vcpu)
{ vcpu.get_vmm()->handle_wfx(vcpu); }


void
Vmm::Guest::handle_ppi(Vcpu_ptr vcpu)
{
  switch (vcpu.hsr().svc_imm())
    {
    case 0: // VGIC IRQ
      _gic.handle_maintenance_irq();
      break;
    default:
      Err().printf("%s: unknown virtual PPI: %d\n", name(),
		   (int)vcpu.hsr().svc_imm());
      break;
    }
}

static void
guest_ppi(Vcpu_ptr vcpu)
{ vcpu.get_vmm()->handle_ppi(vcpu); }

static void guest_irq(Vcpu_ptr vcpu)
{
  vcpu.handle_ipc(vcpu->i.tag, vcpu->i.label, l4_utcb());
}

static void guest_mcrr_access_cp(Vcpu_ptr vcpu, unsigned cp)
{
  using Vmm::Arm::Sys_reg;
  using Key = Sys_reg::Key;
  auto hsr = vcpu.hsr();

  Key k = Key::cp_r_64(cp, hsr.mcrr_opc1(), hsr.mcr_crm());

  auto r = vcpu.get_vmm()->sys_reg(k);
  if (!r)
    {
      Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
        .printf("%08lx: %s p%u, %d, r%d, c%d, c%d, %d (hsr=%08lx)\n",
                vcpu->r.ip, hsr.mcr_read() ? "MRC" : "MCR", cp,
                (unsigned)hsr.mcr_opc1(),
                (unsigned)hsr.mcr_rt(),
                (unsigned)hsr.mcr_crn(),
                (unsigned)hsr.mcr_crm(),
                (unsigned)hsr.mcr_opc2(),
                (l4_umword_t)hsr.raw());
    }
  else if (hsr.mcr_read())
    {
      l4_uint64_t v = r->read(vcpu, k);
      vcpu.set_gpr(hsr.mcr_rt(), v & 0xffffffff);
      vcpu.set_gpr(hsr.mcrr_rt2(), v >> 32);
    }
  else
    {
      l4_uint64_t v = (vcpu.get_gpr(hsr.mcr_rt()) & 0xffffffff)
                      | (((l4_uint64_t)vcpu.get_gpr(hsr.mcrr_rt2())) << 32);

      r->write(vcpu, k, v);
    }

  vcpu.jump_instruction();
}

template<unsigned CP>
static void guest_mcrr_access_cp(Vcpu_ptr vcpu)
{ guest_mcrr_access_cp(vcpu, CP); }

static void guest_mcr_access_cp(Vcpu_ptr vcpu, unsigned cp)
{
  using Vmm::Arm::Sys_reg;
  using Key = Sys_reg::Key;
  auto hsr = vcpu.hsr();

  Key k = Key::cp_r(cp, hsr.mcr_opc1(),
                    hsr.mcr_crn(),
                    hsr.mcr_crm(),
                    hsr.mcr_opc2());

  auto r = vcpu.get_vmm()->sys_reg(k);
  if (!r)
    Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
      .printf("%08lx: %s p%u, %d, r%d, c%d, c%d, %d (hsr=%08lx)\n",
              vcpu->r.ip, hsr.mcr_read() ? "MRC" : "MCR", cp,
              (unsigned)hsr.mcr_opc1(),
              (unsigned)hsr.mcr_rt(),
              (unsigned)hsr.mcr_crn(),
              (unsigned)hsr.mcr_crm(),
              (unsigned)hsr.mcr_opc2(),
              (l4_umword_t)hsr.raw());
  else if (hsr.mcr_read())
    vcpu.set_gpr(hsr.mcr_rt(), r->read(vcpu, k));
  else
    r->write(vcpu, k, vcpu.get_gpr(hsr.mcr_rt()));

  vcpu.jump_instruction();
}

template<unsigned CP>
static void guest_mcr_access_cp(Vcpu_ptr vcpu)
{ guest_mcr_access_cp(vcpu, CP); }

static void guest_msr_access(Vcpu_ptr vcpu)
{
  using Vmm::Arm::Sys_reg;
  using Key = Sys_reg::Key;
  auto hsr = vcpu.hsr();

  Key k = Key::sr(hsr.msr_op0(),
                  hsr.msr_op1(),
                  hsr.msr_crn(),
                  hsr.msr_crm(),
                  hsr.msr_op2());

  auto r = vcpu.get_vmm()->sys_reg(k);
  if (!r)
    {
      if (hsr.msr_read())
        Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
          .printf("%08lx: mrs r%u, S%u_%u_C%u_C%u_%u (hsr=%08lx)\n",
                  vcpu->r.ip, (unsigned)hsr.msr_rt(),
                  (unsigned)hsr.msr_op0(),
                  (unsigned)hsr.msr_op1(),
                  (unsigned)hsr.msr_crn(),
                  (unsigned)hsr.msr_crm(),
                  (unsigned)hsr.msr_op2(),
                  (l4_umword_t)hsr.raw());
      else
        Dbg(Dbg::Core, Dbg::Info, vcpu.get_vmm()->name())
          .printf("%08lx: msr S%u_%u_C%u_C%u_%u = %08lx (hsr=%08lx)\n",
                  vcpu->r.ip,
                  (unsigned)hsr.msr_op0(),
                  (unsigned)hsr.msr_op1(),
                  (unsigned)hsr.msr_crn(),
                  (unsigned)hsr.msr_crm(),
                  (unsigned)hsr.msr_op2(),
                  vcpu.get_gpr(hsr.msr_rt()),
                  (l4_umword_t)hsr.raw());
    }
  else if (hsr.msr_read())
    vcpu.set_gpr(hsr.msr_rt(), r->read(vcpu, k));
  else
    r->write(vcpu, k, vcpu.get_gpr(hsr.msr_rt()));

  vcpu.jump_instruction();
}

void Vmm::Guest::handle_ex_regs_exception(Vcpu_ptr)
{
  state(Stopped);
}

static void ex_regs_exception(Vcpu_ptr vcpu)
{
  vcpu.get_vmm()->handle_ex_regs_exception(vcpu);
}

l4_msgtag_t prepare_guest_entry(Vcpu_ptr vcpu)
{ return vcpu.get_vmm()->handle_entry(vcpu); }

} // namespace

using namespace Vmm;
Entry Vmm::vcpu_entries[64] =
{
  [0x00] = guest_unknown_fault,
  [0x01] = guest_wfx,
  [0x02] = guest_unknown_fault,
  [0x03] = guest_mcr_access_cp<15>,
  [0x04] = guest_mcrr_access_cp<15>,
  [0x05] = guest_mcr_access_cp<14>,
  [0x06] = guest_unknown_fault,
  [0x07] = guest_unknown_fault,
  [0x08] = guest_unknown_fault,
  [0x09] = guest_unknown_fault,
  [0x0a] = guest_unknown_fault,
  [0x0b] = guest_unknown_fault,
  [0x0c] = guest_mcrr_access_cp<14>,
  [0x0d] = guest_unknown_fault,
  [0x0e] = guest_unknown_fault,
  [0x0f] = guest_unknown_fault,
  [0x10] = guest_unknown_fault,
  [0x11] = guest_unknown_fault,
  [0x12] = dispatch_vm_call,
  [0x13] = dispatch_smc,
  [0x14] = guest_unknown_fault,
  [0x15] = guest_unknown_fault,
  [0x16] = dispatch_vm_call,
  [0x17] = dispatch_smc,
  [0x18] = guest_msr_access,
  [0x19] = guest_unknown_fault,
  [0x1a] = guest_unknown_fault,
  [0x1b] = guest_unknown_fault,
  [0x1c] = guest_unknown_fault,
  [0x1d] = guest_unknown_fault,
  [0x1e] = guest_unknown_fault,
  [0x1f] = guest_unknown_fault,
  [0x20] = guest_memory_fault,
  [0x21] = guest_unknown_fault,
  [0x22] = guest_unknown_fault,
  [0x23] = guest_unknown_fault,
  [0x24] = guest_memory_fault,
  [0x25] = guest_unknown_fault,
  [0x26] = guest_unknown_fault,
  [0x27] = guest_unknown_fault,
  [0x28] = guest_unknown_fault,
  [0x29] = guest_unknown_fault,
  [0x2a] = guest_unknown_fault,
  [0x2b] = guest_unknown_fault,
  [0x2c] = guest_unknown_fault,
  [0x2d] = guest_unknown_fault,
  [0x2e] = guest_unknown_fault,
  [0x2f] = guest_unknown_fault,
  [0x30] = guest_unknown_fault,
  [0x31] = guest_unknown_fault,
  [0x32] = guest_unknown_fault,
  [0x33] = guest_unknown_fault,
  [0x34] = guest_unknown_fault,
  [0x35] = guest_unknown_fault,
  [0x36] = guest_unknown_fault,
  [0x37] = guest_unknown_fault,
  [0x38] = guest_unknown_fault,
  [0x39] = guest_unknown_fault,
  [0x3a] = guest_unknown_fault,
  [0x3b] = guest_unknown_fault,
  [0x3c] = guest_unknown_fault,
  [0x3d] = guest_ppi,
  [0x3e] = ex_regs_exception,
  [0x3f] = guest_irq
};
