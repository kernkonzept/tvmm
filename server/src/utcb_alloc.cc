/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

#include <l4/re/env>

#include "debug.h"
#include "utcb_alloc.h"

namespace {

l4_addr_t utcb_area_start;
l4_addr_t utcb_area_end;

}

l4_addr_t alloc_utcb(size_t size)
{
  if (!utcb_area_start)
    {
      l4_fpage_t utcb_area = L4Re::Env::env()->utcb_area();
      utcb_area_start = L4Re::Env::env()->first_free_utcb();
      utcb_area_end = l4_fpage_memaddr(utcb_area)
                      + (1UL << l4_fpage_size(utcb_area));
    }

  if (utcb_area_end - utcb_area_start < size)
    Fatal().abort("No UTCB left!");

  l4_addr_t ret = utcb_area_start;
  utcb_area_start += size;
  return ret;
}
