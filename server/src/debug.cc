/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
/*
 * Copyright (C) 2015-2022 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *            Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <l4/sys/err.h>

#include "debug.h"

#if defined(CONFIG_TVMM_VERBOSITY_WARN) || defined(CONFIG_TVMM_VERBOSITY_ALL)

namespace {

struct Verbosity_level {
  char const *name;
  unsigned mask;
};

Verbosity_level const verbosity_levels[] = {
  { "quiet", Dbg::Quiet },
  { "warn", Dbg::Warn },
  { "info", Dbg::Warn | Dbg::Info },
  { "trace", Dbg::Warn | Dbg::Info | Dbg::Trace }
};

bool verbosity_mask_from_string(char const *str, unsigned *mask)
{
  for (auto const &verbosity_level : verbosity_levels)
    {
      if (strcmp(verbosity_level.name, str) == 0)
        {
          *mask = verbosity_level.mask;
          return true;
        }
    }

  return false;
}

char const *const component_names[] =
  { "core", "cpu", "mmio", "irq", "dev", nullptr };

bool component_from_string(char const *str, size_t len, unsigned *c)
{
  for (unsigned i = 0; i < Dbg::Max_component; ++i)
    {
      if (len == strlen(component_names[i])
          && memcmp(component_names[i], str, len) == 0)
        {
          *c = i;
          return true;
        }
    }

  return false;
}

} // namespace

unsigned long Dbg::level = 1;

void
Dbg::tag() const
{
  if (_instance)
    ::dprintf(1, "tvmm: %s[%s]: ", component_names[_c], _instance);
  else
    ::dprintf(1, "tvmm: %s: ", component_names[_c]);
}

int
Dbg::printf_impl(char const *fmt, ...) const
{
  tag();

  int n;
  va_list args;

  va_start    (args, fmt);
  n = vdprintf (1, fmt, args);
  va_end      (args);

  return n;
}

int
Dbg::vprintf_impl(char const *fmt, va_list args) const
{
  tag();
  return vdprintf(1, fmt, args);
}

int
Dbg::set_verbosity(char const *str)
{
  unsigned mask = 0;

  // ignore leading whitespace
  while(*str && *str == ' ')
    ++str;

  if (verbosity_mask_from_string(str, &mask))
    {
      set_verbosity(mask);
      return L4_EOK;
    }

  char const *eq = strchr(str, '=');
  if (!eq)
    return -L4_EINVAL;

  unsigned c;
  if (!component_from_string(str, eq - str, &c)
      || !verbosity_mask_from_string(eq + 1, &mask))
    {
      return -L4_EINVAL;
    }

  set_verbosity(c, mask);

  return L4_EOK;
}
#endif

#if defined(CONFIG_TVMM_VERBOSITY_ERROR) \
    || defined(CONFIG_TVMM_VERBOSITY_WARN) \
    || defined(CONFIG_TVMM_VERBOSITY_ALL)
int
Err::printf(char const *fmt, ...)
{
  int n;
  va_list args;

  va_start    (args, fmt);
  n = vdprintf (1, fmt, args);
  va_end      (args);

  return n;
}
#endif

int Fatal::abort(char const *msg)
{
  write(2, "FATAL: ", 7);
  write(2, msg, strlen(msg));
  _exit(1);
}
