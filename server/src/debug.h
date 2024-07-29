/*
 * Copyright (C) 2022-2024 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
/*
 * (c) 2013-2014 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <l4/sys/err.h>

struct Fatal
{
  int L4_NORETURN abort(char const *msg);
};

#if defined(CONFIG_TVMM_VERBOSITY_ERROR) \
    || defined(CONFIG_TVMM_VERBOSITY_WARN) \
    || defined(CONFIG_TVMM_VERBOSITY_ALL)
struct Err
{
  static int printf(char const *fmt, ...)
    __attribute__((format(printf,1,2)));
};
#else
struct Err
{
  static int printf(char const * /*fmt*/, ...)
    __attribute__((format(printf, 1, 2)))
  { return 0; }
};
#endif

class Dbg
{
public:
  /// Verbosity level per component.
  enum Verbosity : unsigned long
  {
    Quiet = 0,
#if defined(CONFIG_TVMM_VERBOSITY_WARN) || defined(CONFIG_TVMM_VERBOSITY_ALL)
    Warn = 1,
#else
    Warn = Quiet,
#endif
#if defined(CONFIG_TVMM_VERBOSITY_ALL)
    Info = 2,
    Trace = 4,
#else
    Info = Quiet,
    Trace = Quiet,
#endif
  };

  /**
   * Different components for which the verbosity can be set independently.
   */
  enum Component
  {
    Core = 0,
    Cpu,
    Mmio,
    Irq,
    Dev,
    Max_component
  };

#if defined(CONFIG_TVMM_VERBOSITY_WARN) || defined(CONFIG_TVMM_VERBOSITY_ALL)
  static unsigned long level;

  enum
  {
    Verbosity_shift = 3, /// Bits per component for verbosity
    Verbosity_mask = (1UL << Verbosity_shift) - 1
  };

  static_assert(Max_component * Verbosity_shift <= sizeof(level) * 8,
                "Too many components for level mask");

  /**
   * Set the verbosity for all components to the given levels.
   *
   * \param mask  Mask of verbosity levels.
   */
  static void set_verbosity(unsigned mask)
  {
    for (unsigned i = 0; i < Max_component; ++i)
      set_verbosity(i, mask);
  }

  /**
   * Set the verbosity of a single component to the given level.
   *
   * \param c     Component for which to set verbosity.
   * \param mask  Mask of verbosity levels.
   */
  static void set_verbosity(unsigned c, unsigned mask)
  {
    level &= ~(Verbosity_mask << (Verbosity_shift * c));
    level |= (mask & Verbosity_mask) << (Verbosity_shift * c);
  }

  Dbg(Component c = Core, Verbosity v = Warn, char const *instance = 0)
  : _instance(instance), _mask(v << (Verbosity_shift * c)), _c(c)
  {}

  /**
   * Set debug level according to a verbosity string.
   *
   * The string may either set a global verbosity level:
   *   quiet, warn, info, trace
   *
   * Or it may set the verbosity level for a component:
   *
   *   <component>=<level>
   *
   * where component is one of: core, cpu, mmio, irq, dev, pm, vbus_event
   * and level the same as above.
   *
   * To change the verbosity of multiple components repeat
   * the verbosity switch.
   *
   * \retval L4_EOK      operation succeeded
   * \retval -L4_EINVAL  invalid verbosity string
   *
   * Example:
   *
   *  uvmm -D info -D irq=trace
   *
   *    Sets verbosity for all components to info except for
   *    IRQ handling which is set to trace.
   *
   *  uvmm -D trace -D dev=warn -D mmio=warn
   *
   *    Enables tracing for all components except devices
   *    and mmio.
   *
   */
  static int set_verbosity(char const *str);

  int __attribute__((always_inline, format(printf, 2, 3)))
  printf(char const *fmt, ...) const
  {
    if (!(level & _mask))
      return 0;

#ifdef __clang__
    // Clang lacks __builtin_va_arg_pack :'(
    va_list args;
    va_start(args, fmt);
    int ret = vprintf_impl(fmt, args);
    va_end(args);
    return ret;
#else
    return printf_impl(fmt, __builtin_va_arg_pack());
#endif
  }

private:
  void tag() const;

  int printf_impl(char const *fmt, ...) const
    __attribute__((format(printf, 2, 3)));
  int vprintf_impl(char const *fmt, va_list args) const
    __attribute__((format(printf, 2, 0)));

  char const *_instance;
  unsigned short _mask;
  unsigned char _c;

  static_assert(Max_component * Verbosity_shift <= sizeof(_mask) * 8,
                "Too many components for level mask");
#else
  static void set_verbosity(unsigned, unsigned) {}
  static void set_verbosity(unsigned) {}
  static int set_verbosity(char const *) { return L4_EOK; }

  Dbg(Component c = Core, Verbosity v = Warn, char const *instance = "")
  {
    (void)c;
    (void)v;
    (void)instance;
  }

  int __attribute__((always_inline, format(printf, 2, 3)))
  printf(char const *, ...) const
  { return 0; }
#endif
};
