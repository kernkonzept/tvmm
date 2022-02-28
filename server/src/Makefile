PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

include $(L4DIR)/mk/Makeconf

SYSTEMS         = arm-l4f
TARGET          = tvmm
MODE            = sigma0
VARIANTS        = nofpu

REQUIRES_LIBS   = libc_minimal libc_minimal_l4re libc_be_minimal_log_io \
                  l4sys l4util l4re l4re-util
DEFINES        += -DL4_CXX_NO_EXCEPTION_BACKTRACE -DL4_MINIMAL_LIBC -DL4_NO_RTTI
CXXFLAGS       += -fno-rtti -fno-exceptions

CXXFLAGS       += $(CXXFLAGS-$(ARCH))

SRC_CC          = main.cc generic_guest.cc \
                  generic_cpu_dev.cc generic_vcpu_ptr.cc \
                  ARCH-$(ARCH)/cpu_dev.cc \
                  mmio_device.cc \
                  vm_memmap.cc irq_svr.cc debug.cc utcb_alloc.cc

SRC_CC-arm-common = arm/guest_arm.cc arm/cpu_dev_arm.cc \
                    arm/gic-v3.cc ARCH-$(ARCH)/guest_subarch.cc

ifeq ($(CONFIG_TVMM_FAULT_INJECT),y)
SRC_CC-arm-common += ARCH-$(ARCH)/guest_arm_exc.cc
else
SRC_CC-arm-common += arm/guest_arm_noexc.cc
endif

SRC_CC-arm   = $(SRC_CC-arm-common) ARCH-arm/vcpu_ptr.cc
SRC_CC-arm64 = $(SRC_CC-arm-common)

SRC_CC-arm-$(CONFIG_TVMM_VDEV_PSCI)   += device/arm/psci.cc
SRC_CC-arm64-$(CONFIG_TVMM_VDEV_PSCI) += device/arm/psci.cc

SRC_CC        += $(SRC_CC-$(ARCH))
SRC_CC        += $(SRC_CC-$(ARCH)-y)

PRIVATE_INCDIR  = $(SRC_DIR)/../include $(SRC_DIR) $(SRC_DIR)/ARCH-$(ARCH)
PRIVATE_INCDIR-arm   += $(SRC_DIR)/arm $(SRC_DIR)/device/arm
PRIVATE_INCDIR-arm64 += $(SRC_DIR)/arm $(SRC_DIR)/device/arm
PRIVATE_INCDIR += $(PRIVATE_INCDIR-$(ARCH))

include $(L4DIR)/mk/prog.mk

DEFAULT_HEAP_SIZE := $(CONFIG_TVMM_HEAP_SIZE)

# Force armv7+ve on pre-armv8 AArch32 build trees.
ifeq ($(ARCH)-$(CONFIG_CPU_ARMV8PLUS),arm-)
CARCHFLAGS := $(filter-out -march%,$(CARCHFLAGS)) $(GCCARMV7VEFPOPT_arm)
endif
