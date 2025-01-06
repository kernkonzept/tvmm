/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2022, 2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */

#include "generic_vcpu_ptr.h"
#include "guest.h"

namespace Vmm {

char const *Generic_vcpu_ptr::get_vmm_name()
{ return get_vmm()->name(); }

}
