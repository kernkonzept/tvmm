/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017-2020, 2024 Kernkonzept GmbH.
 * Author(s): Alexander Warg <alexander.warg@kernkonzept.com>
 *
 */

#pragma once

namespace Vmm {

enum { Guest_64bit_supported = false };

void
Guest::add_sys_reg_aarch64(unsigned, unsigned,
                           unsigned, unsigned,
                           unsigned,
                           Sys_reg *)
{
}

}
