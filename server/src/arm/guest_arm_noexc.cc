/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2021, 2024 Kernkonzept GmbH.
 * Author(s): Georg Kotheimer <georg.kotheimer@kernkonzept.com>
 */

#include "guest.h"

namespace Vmm {

bool Guest::inject_abort(Vcpu_ptr, bool, l4_addr_t)
{
  return false;
}

bool Guest::inject_undef(Vcpu_ptr)
{
  return false;
}

}
