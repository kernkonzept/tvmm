# L4Re tiny vmm

tvmm is a virtual machine monitor for the L4Re operating system when running on
MPU based systems. It implements just the bare minimum to let a guest run on
Arm with hardware pass-through. All resources must be prepared offline and must
be passed to tvmm by the loader (tinit).

Features:

* Can host multiple VMs
* VMs can only have a single vCPU
* GICv3 only
* Distributor and redistributor address defined at compile time
* HVC putc() call

# Command line options

* `D<verbosity>` - set debugging verbosity
* `V<task>[:<thread>]` - define a new VM

  The new VM will use the `<task>` capability for the VCPu use mode. The vCPU
  itself will use the `<thread>` thread capability. If omitted, the main thread
  of tvmm will be reused as vCPU thread. This must be used obviously only once.

Options that work on the currently defined VM:

* `C<cap>` - bind VM-control IPC interface to `<cap>`. Requires
  `CONFIG_TVMM_VM_CTRL_IFC` to be enabled.
* `E<address>` - set vCPU entry point address
* `I<cap>:<num>` - IRQ pass through

  Forward interrupts from the `<cap>` L4::Irq capability to the VM as interrupt
  `<num>`.
* `L<address>` - load guest ELF image from `<address>`. Requires
  `CONFIG_TVMM_ELF_LOADER` to be enabled.

Options that are ignored because the feature is not implemented yet:

* `P<min>:<max>` - define IRQ priority mapping range

  The guests interrupt priorities are mapped to the range `<min>`..`<max>`
  L4Re thread priorities.

# Documentation

This package is part of the L4Re operating system. For documentation and
build instructions please refer to [l4re.org](https://l4re.org).

# Contributions

We welcome contributions. Please see the
[contributors guide](https://l4re.org/contributing/).

# License

Detailed licensing and copyright information can be found in the
[LICENSE](LICENSE.spdx) file.
