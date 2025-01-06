/*
 * Copyright (C) 2015, 2024 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */
#pragma once

namespace Gic {

/**
 * Interface for handlers of end-of-interrupt messages.
 *
 * This is the generic interface for notifications from the
 * interrupt controller to an interrupt-emitting device.
 */
struct Eoi_handler
{
  virtual void eoi() = 0;
  virtual void set_priority(unsigned prio) = 0;
protected:
  virtual ~Eoi_handler() = default;
};

/**
 * Interface for handlers of irqs that are directly injected into the vCPU.
 */
struct Virq_handler : public Eoi_handler
{
  virtual void configure(l4_umword_t cfg) = 0;
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual void set_pending() = 0;
  virtual void clear_pending() = 0;

protected:
  virtual ~Virq_handler() = default;
};

/**
 * Generic interrupt controller interface.
 */
struct Ic
{
  virtual ~Ic() = default;

  virtual void set(unsigned irq) = 0;
  virtual void clear(unsigned irq) = 0;

  /**
   * Register a device source for forwarding downstream events.
   *
   * Only one device source can be registered, throws a runtime
   * exception if the irq source is already bound
   *
   * \param irq Irq number to connect the listener to.
   * \param src Device source. If the irq is already bound it needs to
   *            be the same device source as the already registered one.
   *            Set to nullptr to unbind a registered handler.
   *
   * \note The caller is responsible to ensure that the eoi handler is
   *       unbound before it is destructed.
   */
  virtual void bind_eoi_handler(unsigned irq, Eoi_handler *src) = 0;

  virtual void bind_virq_handler(unsigned irq, Virq_handler *src) = 0;

  virtual void bind_cpulocal_virq_handler(unsigned irq, Virq_handler *src) = 0;

  /**
   * Get the irq source currently bound to irq
   *
   * \param irq Irq number
   * \return Irq source currently bound to irq
   */
  virtual Eoi_handler *get_eoi_handler(unsigned irq) const = 0;
};

} // namespace
