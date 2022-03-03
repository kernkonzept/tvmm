# tVMM

Implements just the bare minimum to let a guest run on Arm with just HW-pass
through. All resources must be prepared offline and must be passed to tvmm.
Features:

* Can host multiple VMs
* VMs can only have a single vCPU
* GICv3 only
* Distributor and redistributor address defined at compile time
* HVC putc() call
* IRQ priorities support

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
* `P<min>:<max>` - define IRQ priority mapping range
  
  The guests interrupt priorities are mapped to the range `<min>`..`<max>`
  L4Re thread priorities.
