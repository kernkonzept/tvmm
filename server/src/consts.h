/*
 * Copyright (C) 2017, 2024 Kernkonzept GmbH.
 * Author(s): Philipp Eppelt <philipp.eppelt@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

namespace Vmm {

enum Handler_return_codes
{
  Retry = 0,
  Jump_instr = 1,
};

} // namespace Vmm
