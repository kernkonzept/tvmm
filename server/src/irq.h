/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
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
  virtual void bind(L4::Cap<L4::Thread> thread, l4_umword_t cfg) = 0;
  virtual void unbind() = 0;
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

namespace Vmm {

/**
 * Generic interrupt line on an interrupt controller.
 *
 * The Irq_sink implements a line-triggered interrupt and
 * remembers it's current state. It will only notify the
 * interrupt controller when it's state changes, thus effectively
 * ignoring multiple inject() or ack().
 */
class Irq_sink
{
public:
  Irq_sink() : _ic(nullptr), _state(false) {}

  Irq_sink(Gic::Ic *ic, unsigned irq)
  : _irq(irq), _ic(ic), _state(false)
  {}

  Irq_sink(Irq_sink const &) = delete;
  Irq_sink(Irq_sink &&other) = delete;

  void rebind(Gic::Ic *ic, unsigned irq)
  {
    ack();

    _ic = ic;
    _irq = irq;
  }

  void inject()
  {
    if (_state || !_ic)
      return;

    _state = true;
    _ic->set(_irq);
  }

  void ack()
  {
    if (!_state || !_ic)
      return;

    _ic->clear(_irq);
    _state = false;
  }

  /**
   * Set the given end-of-interrupt handler at the connected IC.
   *
   * \param handler  Handler to set for EOI notification.
   *
   * The function is only a forwarder to Ic::bind_eoi_handler(), the
   * handler must still be managed by the caller. In particular, the caller
   * must make sure that the handler is unbound before the Irq_sink
   * object is destructed.
   *
   * If no IC has been bound yet, the function does nothing.
   */
  void set_eoi_handler(Gic::Eoi_handler *handler) const
  {
    if (_ic)
      _ic->bind_eoi_handler(_irq, handler);
  }

  void set_virq_handler(Gic::Virq_handler *handler) const
  {
    if (_ic)
      _ic->bind_virq_handler(_irq, handler);
  }

private:
  unsigned _irq;
  Gic::Ic *_ic;
  bool _state;
};

/**
 * Generic interrupt line on an interrupt controller.
 *
 * The Irq_edge_sink implements an edge-triggered interrupt.
 * It notifies the interrupt controller on each inject.
 */
class Irq_edge_sink
{
public:
  Irq_edge_sink() = default;

  Irq_edge_sink(Gic::Ic *ic, unsigned irq)
  : _irq(irq), _ic(ic)
  {}

  void rebind(Gic::Ic *ic, unsigned irq)
  {
    _ic = ic;
    _irq = irq;
  }

  void inject()
  { _ic->set(_irq); }

  void set_eoi_handler(Gic::Eoi_handler *handler) const
  {
    if (_ic)
      _ic->bind_eoi_handler(_irq, handler);
  }

  unsigned irq() const { return _irq; }
  Gic::Ic* ic() const { return _ic; }

private:
  unsigned _irq;
  Gic::Ic *_ic;
};

} // namespace
