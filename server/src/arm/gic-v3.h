/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2013-2024 Kernkonzept GmbH.
 * Author(s): Alexander Warg <alexander.warg@kernkonzept.com>
 *            Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */

#pragma once

#include <l4/re/env>
#include <l4/sys/l4int.h>
#include <l4/cxx/bitfield>
#include <l4/cxx/utils>
#include <l4/cxx/avl_map>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "mmio_device.h"
#include "irq.h"
#include "vcpu_ptr.h"
#include "sys_reg.h"
#include "generic_cpu_dev.h"

namespace Gic {

enum
{
  Num_local = 32,
  Num_spis = (CONFIG_TVMM_GIC_VIRTUAL_SPIS + 31U) & ~31U,
};

struct Prio_mapper
{
  l4_uint8_t mult;
  l4_uint8_t lower;

  l4_uint8_t map_irq_priority(l4_uint8_t prio) const
  {
    return (255U - prio) * mult / 255U + lower;
  }

  void set_irq_priority_range(unsigned min, unsigned max)
  {
    mult = max - min;
    lower = min;
  }
};

/**
 * Item on an Atomic_fwd_list.
 */
class Atomic_fwd_list_item
{
  template<typename T>
  friend class Atomic_fwd_list;

  enum {
    // Element is not in a list. Distinct from nullptr which is is the end of
    // list.
    Mark_deleted = 1,
  };

public:
  Atomic_fwd_list_item() : _next((Atomic_fwd_list_item*)Mark_deleted) {}

  bool in_list() const { return _next != (Atomic_fwd_list_item*)Mark_deleted; }

private:
  Atomic_fwd_list_item(Atomic_fwd_list_item *n) : _next(n) {}

  Atomic_fwd_list_item *_next;
};

template<typename T>
class Atomic_fwd_list
{
public:
  Atomic_fwd_list() : _head(nullptr) {}

  class Iterator
  {
    friend class Atomic_fwd_list;

  public:
    Iterator() : _e(nullptr), _pn(nullptr) {}

    Iterator operator++()
    {
      _pn = &_e->_next;
      _e = _e->_next;
      return *this;
    }

    T *operator*() const { return static_cast<T*>(_e); }
    T *operator->() const { return static_cast<T*>(_e); }

    bool operator==(Iterator const &other) const { return other._e == _e; }
    bool operator!=(Iterator const &other) const { return other._e != _e; }

  private:
    Iterator(Atomic_fwd_list_item **pn, Atomic_fwd_list_item *e)
    : _e(e), _pn(pn) {}
    Iterator(Atomic_fwd_list_item **head) : _e(*head), _pn(head) {}
    Iterator(Atomic_fwd_list_item *e) : _e(e), _pn(nullptr) {}

    Atomic_fwd_list_item *_e;

    // Pointer to _next pointer of previous element that points to _e.
    Atomic_fwd_list_item **_pn;
  };

  Iterator before_begin() { return Iterator(&_head); }
  Iterator begin() { return Iterator(&_head._next); }
  Iterator end() { return Iterator(); }

  /**
   * Add element to front of list.
   */
  void push(T *e)
  {
    Atomic_fwd_list_item *old_next = e->_next;
    if (old_next != (Atomic_fwd_list_item*)Atomic_fwd_list_item::Mark_deleted)
      return;

    Atomic_fwd_list_item *head = _head._next;
    e->_next = head;

    _head._next = static_cast<Atomic_fwd_list_item*>(e);
  }

  /**
   * Swap this and the other list.
   */
  void swap(Atomic_fwd_list &other)
  {
    Atomic_fwd_list_item *cur = _head._next;
    Atomic_fwd_list_item *o = other._head._next;

    other._head._next = cur;
    _head._next = o;
  }

  /**
   * Remove item from list.
   *
   * This method is *not* thread safe. There must be no concurrent
   * manipulations of the list!
   */
  static Iterator erase(Iterator const &e)
  {
    Iterator ret(e._pn, e._e->_next);
    *e._pn = e._e->_next;
    e._e->_next = (Atomic_fwd_list_item*)Atomic_fwd_list_item::Mark_deleted;
    return ret;
  }

  /**
   * Move item from \a other list to this one after \a pos.
   *
   * \return Iterator pointing to element after \a e on the \a other list.
   */
  static Iterator move_after(Iterator const &pos, Atomic_fwd_list& /*other*/,
                             Iterator const &e)
  {
    Iterator ret(e._pn, e._e->_next);

    *e._pn = e._e->_next;
    e._e->_next = pos._e->_next;
    pos._e->_next = e._e;

    return ret;
  }

private:
  Atomic_fwd_list_item _head;
};

class Irq;

class Vcpu_handler
{
public:
  /**
   * Queue Irq as pending on this vCPU.
   *
   * The vCPU is not notified. It is the responsibility of the caller to
   * trigger X-CPU notifications if necessary.
   */
  void queue(Irq *e)
  {
    _pending_irqs.push(e);
  }

  L4::Cap<L4::Thread> thread_cap() const
  { return _thread_cap; }

  void thread_cap(L4::Cap<L4::Thread> cap)
  { _thread_cap = cap; }

protected:
  /// Priority sorted list of pending IRQs owned by this vCPU.
  Atomic_fwd_list<Irq> _owned_pend_irqs;

  L4::Cap<L4::Thread> _thread_cap;

  void fetch_pending_irqs();

private:
  /// The list of pending IRQs for this (or an invalid) vCPU.
  Atomic_fwd_list<Irq> _pending_irqs;
};

class Irq_info
{
private:
  using State = l4_uint16_t;
  State _state = 0;

public:
  CXX_BITFIELD_MEMBER_RO( 0,  0, pending,     _state); // GICD_I[SC]PENDRn
  CXX_BITFIELD_MEMBER_RO( 1,  1, active,      _state); // GICD_I[SC]ACTIVERn
  CXX_BITFIELD_MEMBER_RO( 2,  2, enabled,     _state); // GICD_I[SC]ENABLERn
  CXX_BITFIELD_MEMBER_RO( 3,  4, config,      _state); // GICD_ICFGRn
  CXX_BITFIELD_MEMBER_RO( 5,  5, group,       _state); // GICD_IGROUPRn
  CXX_BITFIELD_MEMBER_RO( 8, 15, prio,        _state); // GICD_IPRIORITYRn

private:
  template<typename BFM, typename STATET, typename VALT>
  static bool atomic_set(STATET *state, VALT v)
  {
    State old = *state;
    State nv;

    nv = BFM::set_dirty(old, v);
    if (old == nv)
      return false;

    *state = nv;
    return true;
  }

  enum
  {
    Pending_and_enabled = pending_bfm_t::Mask | enabled_bfm_t::Mask,
  };

  static bool is_pending_and_enabled(State state)
  { return (state & Pending_and_enabled) == Pending_and_enabled; }

  static bool is_pending_or_enabled(State state)
  { return state & Pending_and_enabled; }

  static bool is_active(State state)
  { return state & active_bfm_t::Mask; }

  Irq_info(Irq_info const &) = delete;
  Irq_info operator = (Irq_info const &) = delete;

  /**
   * Set the pending or enabled bit.
   *
   * \return True if we made the IRQ pending&enabled. False if the IRQ was
   *         already pending&enabled or is not yet pending&enabled.
   */
  bool set_pe(unsigned char set)
  {
    State old = _state;

    if (old & set)
      return false;

    _state = old | set;
    return is_pending_or_enabled(old);
  }

  /**
   * Clear the pending or enabled flag.
   */
  void clear_pe(unsigned char clear)
  {
    _state &= ~clear;
  }

public:
  Irq_info() = default;

  bool enable()
  { return set_pe(enabled_bfm_t::Mask); }

  void disable()
  { return clear_pe(enabled_bfm_t::Mask); }

  bool set_pending()
  { return set_pe(pending_bfm_t::Mask); }

  void clear_pending()
  { return clear_pe(pending_bfm_t::Mask); }

  class Take_result
  {
  public:
    enum Result { Ok, Drop, Keep };
    constexpr Take_result(Result r) : _r(r) {}
    explicit constexpr operator bool() const { return _r == Ok; }
    constexpr bool drop() const { return _r == Drop; }
    constexpr bool keep() const { return _r == Keep; }
  private:
    Result _r;
  };

  Take_result take_on_cpu()
  {
    State old = _state;


    if (!is_pending_and_enabled(old))
      return Take_result::Drop;

    // Already active? Cannot take twice!
    if (is_active(old))
      return Take_result::Keep;

    _state = (old & ~pending_bfm_t::Mask) | active_bfm_t::Mask;
    return Take_result::Ok;
  }

  bool eoi()
  {
    State old = _state;
    _state &= ~active_bfm_t::Mask;
    return is_pending_and_enabled(old);
  }

  bool prio(unsigned char p)
  { return atomic_set<prio_bfm_t>(&_state, p); }

  bool active(bool a)
  { return atomic_set<active_bfm_t>(&_state, a); }

  bool group(bool grp1)
  { return atomic_set<group_bfm_t>(&_state, grp1); }

  bool config(unsigned cfg)
  { return atomic_set<config_bfm_t>(&_state, cfg); }

  bool is_pending_and_enabled() const
  {
    State state = __atomic_load_n(&_state, __ATOMIC_ACQUIRE);
    return is_pending_and_enabled(state);
  }

  void reset()
  { _state = 0; }
};

class Irq : public Atomic_fwd_list_item
{
  struct Config
  {
    l4_uint16_t raw = 0;

    CXX_BITFIELD_MEMBER( 0,  9, id, raw); // interrupt id
    CXX_BITFIELD_MEMBER(10, 15, lr, raw); // current LR
  };

public:
  Irq() = default;

  Irq(const Irq &) = delete;
  Irq(Irq &&) noexcept = delete;
  Irq& operator=(const Irq &) = delete;
  Irq& operator=(Irq &&) noexcept = delete;

  bool enabled() const { return _irq.enabled(); }
  bool pending() const { return _irq.pending(); }
  bool active() const { return _irq.active(); }
  bool group() const { return _irq.group(); }
  unsigned char config() const { return _irq.config(); }
  unsigned char prio() const { return _irq.prio(); }
  unsigned char target() const { return 0; }

  Eoi_handler *get_eoi_handler() const
  {
    return reinterpret_cast<Eoi_handler*>(_eoi & ~(l4_umword_t)1);
  }

  Virq_handler *get_virq_handler() const
  {
    if (_eoi & 1U)
      return reinterpret_cast<Virq_handler*>(_eoi & ~(l4_umword_t)1);
    else
      return nullptr;
  }

  bool is_pending_and_enabled() const { return _irq.is_pending_and_enabled(); }

  unsigned id() const { return _cfg.id(); }
  unsigned lr() const { return _cfg.lr(); }

  void set_eoi(Eoi_handler *eoi)
  { _eoi = reinterpret_cast<l4_umword_t>(eoi); }

  void set_virq(Virq_handler *virq)
  { _eoi = reinterpret_cast<l4_umword_t>(virq) | 1U; }

  void set_id(l4_uint16_t id) { _cfg.id() = id; }

  bool enable(bool ena, Vcpu_handler *vcpu)
  {
    bool ret = false;

    if (ena)
      {
        if (auto *h = get_virq_handler())
          {
            _irq.enable();
            h->enable();
          }
        else if (_irq.enable())
          {
            vcpu->queue(this);
            ret = true;
          }
      }
    else
      {
        if (auto *h = get_virq_handler())
          h->disable();
        _irq.disable();
      }

    return ret;
  }

  bool pending(bool pend, Vcpu_handler *vcpu)
  {
    bool ret = false;

    if (pend)
      {
        if (auto *h = get_virq_handler())
          h->set_pending();
        else if (_irq.set_pending())
          {
            vcpu->queue(this);
            ret = true;
          }
      }
    else
      {
        if (auto *h = get_virq_handler())
          h->clear_pending();
        _irq.clear_pending();
      }

    return ret;
  }

  Irq_info::Take_result take_on_cpu()
  {
    return _irq.take_on_cpu();
  }

  void eoi()
  {
    _irq.eoi();
    if (auto *h = get_eoi_handler())
      h->eoi();
  }

  void prio(unsigned char p, Prio_mapper *m)
  {
    _irq.prio(p);
    auto *eoi = get_eoi_handler();
    if (eoi && m)
      eoi->set_priority(m->map_irq_priority(p));
    reconfigure();
  }

  void group(bool grp1)
  {
    _irq.group(grp1);
    reconfigure();
  }

  void config(unsigned char cfg) { _irq.config(cfg); }

  void set_lr(unsigned idx) { _cfg.lr() = idx; }
  void clear_lr() { set_lr(0); }

  void reset()
  { _irq.reset(); }

  void reinit()
  {
    enable(false, nullptr);
    pending(false, nullptr);
  }

   void reconfigure() const
   {
      auto *h = get_virq_handler();
      if (!h)
        return;

     Vmm::Arm::Gic_h::Vcpu_irq_cfg cfg(0);
     cfg.vid() = _cfg.id();
     cfg.grp1() = group();
     cfg.prio() = prio();

     h->configure(cfg.raw);
   }

private:
  l4_umword_t _eoi = 0;
  Irq_info _irq;
  Config _cfg;
};

using Const_irq = const Irq;

template<unsigned SIZE, unsigned FIRST>
class Irq_array_fixed
{
public:
  using Irq = ::Gic::Irq;
  using Const_irq = ::Gic::Const_irq;

  Irq_array_fixed()
  {
    for (unsigned i = 0; i < SIZE; i++)
      _irqs[i].set_id(i + FIRST);
  }

  Irq &operator [] (unsigned i)
  { return _irqs[i]; }

  Irq const &operator [] (unsigned i) const
  { return _irqs[i]; }

  unsigned size() const { return SIZE; }

  void reinit()
  {
    for (unsigned i = 0; i < SIZE; i++)
      _irqs[i].reinit();
  }

private:
  Irq _irqs[SIZE];
};

template<unsigned FIRST, unsigned MAX>
class Irq_array_dyn
{
  template< typename T>
  struct Malloc_alloc
  {
    T *alloc() noexcept
    { return reinterpret_cast<T*>(malloc(sizeof(T))); }

    void free(T *t) noexcept
    { ::free(t); }
  };

public:
  using Irq = ::Gic::Irq;
  using Const_irq = ::Gic::Const_irq;

  Irq &operator [] (unsigned i)
  {
    auto n = _irqs.find_node(i);
    if (!n)
      {
	_sentinel.reset();
	return _sentinel;
      }

    return const_cast<Irq&>(n->second);
  }

  Irq const &operator [] (unsigned i) const
  {
    auto n = _irqs.find_node(i);
    if (!n)
      {
	_sentinel.reset();
	return _sentinel;
      }

    return n->second;
  }

  Irq &alloc(unsigned i)
  {
    auto n = _irqs.find_node(i);
    if (n)
      return const_cast<Irq&>(n->second);
    else
      {
	Irq &ret = _irqs.emplace(i).first->second;
	ret.set_id(i + FIRST);
	return ret;
      }
  }

  unsigned size() const { return MAX; }

  void reinit()
  {
    for (auto i = _irqs.begin(); i != _irqs.end(); ++i)
      i->second.reinit();
  }

private:
  static Irq _sentinel;
  cxx::Avl_map<unsigned, Irq, cxx::Lt_functor, Malloc_alloc> _irqs;
};

template<unsigned FIRST, unsigned MAX>
Irq Irq_array_dyn<FIRST, MAX>::_sentinel;

typedef Irq_array_fixed<Num_local, 0> Ppi_irq_array;
typedef Irq_array_dyn<Num_local, Num_spis> Spi_irq_array;

///////////////////////////////////////////////////////////////////////////////
// GIC CPU interface
class Cpu : public Vcpu_handler
{
  struct Lr
  {
    enum State
    {
      Empty              = 0,
      Pending            = 1,
      Active             = 2,
      Active_and_pending = 3
    };

    l4_uint64_t raw;
    Lr() = default;
    explicit Lr(l4_uint64_t v) : raw(v) {}
    CXX_BITFIELD_MEMBER(  0, 31, vid, raw);
    CXX_BITFIELD_MEMBER( 32, 41, pid, raw);
    CXX_BITFIELD_MEMBER( 41, 41, eoi, raw);
    CXX_BITFIELD_MEMBER( 48, 55, prio, raw);
    CXX_BITFIELD_MEMBER( 60, 60, grp1, raw);
    CXX_BITFIELD_MEMBER( 61, 61, hw, raw);
    CXX_BITFIELD_MEMBER( 62, 63, state, raw);
    CXX_BITFIELD_MEMBER( 62, 62, pending, raw);
    CXX_BITFIELD_MEMBER( 63, 63, active, raw);

    void set_cpuid(unsigned) {}
  };

  static Lr read_lr(Vmm::Vcpu_ptr vcpu, unsigned idx)
  {
    return Lr(l4_vcpu_e_read_64(*vcpu, L4_VCPU_E_GIC_V3_LR0 + idx * 8));
  }

  static void write_lr(Vmm::Vcpu_ptr vcpu, unsigned idx, Lr lr)
  { l4_vcpu_e_write_64(*vcpu, L4_VCPU_E_GIC_V3_LR0 + idx * 8, lr.raw); }

  static unsigned pri_mask(Vmm::Vcpu_ptr vcpu)
  {
    l4_uint32_t v = l4_vcpu_e_read_32(*vcpu, L4_VCPU_E_GIC_VMCR);
    return (v >> 24);
  }

  struct Irq_sentinel : public L4::Irqep_t<Irq_sentinel>
  {
  public:
    void handle_irq() {}
  };

  static Irq_sentinel _irq_sentinel;

public:
  using Irq = ::Gic::Irq;
  using Const_irq = ::Gic::Const_irq;

  enum { Num_lrs = 4, Lr_mask = (1UL << Num_lrs) - 1U, };

  static_assert(Num_lrs <= 32, "Can only handle up to 32 list registers.");

  Cpu(Spi_irq_array *spis)
  : _spis(spis)
  {}

  /// Tell the kernel our "vgic prio to irq prio" mapping.
  void setup_prio_cfg(l4_uint8_t mult, l4_uint8_t lower)
  {
    Vmm::Arm::Gic_h::Irq_prio_cfg cfg(0);
    cfg.mult() = mult;
    cfg.base() = lower;
    cfg.map() = 1;
#ifdef IRQ_PRIORITIES_NOT_IMPLEMENTED_YET
    l4_vcpu_e_write_32(*_vcpu, L4_VCPU_E_GIC_PRIO, cfg.raw);
#endif
  }

  /// Get the number of CPU local IRQs
  static unsigned num_local() { return Num_local; }


  /// Get if this is a valid CPU interface (with a vCPU.
  bool is_valid() const { return *_vcpu; }

  /**
   * Get the Processor_Number + Affinity_Value of GICv3+ GICR_TYPER.
   *
   * If this his not a valid CPU interface affinity willbe 0xffffffff.
   */
  l4_uint64_t get_typer() const
  {
    if (is_valid())
      return (((l4_uint64_t) /*_vcpu.get_vcpu_id()*/ 0) << 8)
             | (((l4_uint64_t)affinity()) << 32);

    return 0xffffffff00000000;
  }

  /// get the local IRQ for irqn (irqn < 32)
  Irq& local_irq(unsigned irqn) { return _local_irq[irqn]; }
  /// get the array of local IRQs of this CPU
  Ppi_irq_array &local_irqs() { return _local_irq; }
  Ppi_irq_array const &local_irqs() const { return _local_irq; }

  /*
   * Get empty list register
   *
   * We might try to preempt a lower priority interrupt from the
   * link registers here. But since our main guest does not use
   * priorities we ignore this possibility.
   *
   * \return Returns 0 if no empty list register is available, (lr_idx
   *         + 1) otherwise
   */
  unsigned get_empty_lr() const
  { return __builtin_ffs(l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_ELSR) & Lr_mask); }

  /// return if there are pending IRQs in the LRs
  bool pending_irqs() const
  {
    l4_uint32_t elsr = l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_ELSR) & Lr_mask;
    return elsr != Lr_mask;
  }

  /// Get in Irq for the given `intid`, works for SGIs, PPIs, and SPIs
  Irq& irq_from_intid(unsigned intid)
  {
    if (intid < Num_local)
      return _local_irq[intid];
    else
      return (*_spis)[intid - Num_local];
  }

  void vcpu(Vmm::Vcpu_ptr vcpu) { _vcpu = vcpu; }

  /// Get the associated vCPU
  Vmm::Vcpu_ptr vcpu() const { return _vcpu; }

  /// Handle pending EOIs
  void handle_eois()
  {
    using namespace Vmm::Arm;
    Gic_h::Misr misr(l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_MISR));

    if (!misr.eoi())
      return;

    l4_uint32_t eisr = l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_EISR);
    if (!eisr)
      return;

    for (unsigned i = 0; i < Num_lrs; ++i, eisr >>= 1)
      {
        if (!(eisr & 1))
          continue;

        Lr lr = read_lr(_vcpu, i);
        Irq &c = irq_from_intid(lr.vid());
        assert(lr.state() == Lr::Empty);

        c.clear_lr();
        c.eoi();
        write_lr(_vcpu, i, Lr(0));
        _set_elsr(1U << i);
      }

    // all EOIs are handled
    l4_vcpu_e_write_32(*_vcpu, L4_VCPU_E_GIC_EISR, 0);
    misr.eoi() = 0;
    l4_vcpu_e_write_32(*_vcpu, L4_VCPU_E_GIC_MISR, misr.raw);
  }

  void add_pending_irq(unsigned lr, Irq &irq)
  {
    Lr new_lr(0);
    new_lr.state() = Lr::Pending;
    new_lr.eoi()   = 1; // need an EOI IRQ
    new_lr.vid()   = irq.id();
    new_lr.set_cpuid(0);
    new_lr.prio()  = irq.prio();
    new_lr.grp1()  = irq.group();

    // uses 0 for "no link register assigned" (see #get_empty_lr())
    irq.set_lr(lr + 1);
    write_lr(_vcpu, lr, new_lr);
    _clear_elsr(1U << lr);
  }

  bool inject(Irq &irq)
  {
    // free LRs if there are inactive LRs
    handle_eois();

    unsigned lr_idx = get_empty_lr();
    if (!lr_idx)
      return false;

    if (!irq.take_on_cpu())
      return false;

    add_pending_irq(lr_idx - 1, irq);
    return true;
  }

  /// Handle pending vGIC maintenance IRQs
  void handle_maintenance_irq()
  { handle_eois(); }

  /**
   * Find and take a pending&enabled IRQ targeting this CPU.
   *
   * If an Irq is returned it *must* be added to a Lr. The Irq will already be
   * marked as active.
   */
  Irq *take_pending_irq(unsigned char min_prio)
  {
    bool rescan;
    do
      {
        rescan = false;
        fetch_pending_irqs();

        for (auto it = _owned_pend_irqs.begin(); it != _owned_pend_irqs.end();)
          {
            if (it->prio() >= min_prio)
              break;

            auto took = it->take_on_cpu();
            if (took)
              {
                Irq *ret = *it;
                _owned_pend_irqs.erase(it);
                if (ret->is_pending_and_enabled())
                  queue_and_notify(ret);
                return ret;
              }
            else if (took.drop())
              {
                Irq *removed = *it;
                it = _owned_pend_irqs.erase(it);
                if (removed->is_pending_and_enabled())
                  rescan = queue_and_notify(removed) || rescan;
              }
            else
              ++it;
          }
      }
    while (rescan);

    return nullptr;
  }

  /// read GICV_HCR / ICH_HCR_EL2
  Vmm::Arm::Gic_h::Hcr hcr() const
  {
    using Vmm::Arm::Gic_h::Hcr;
    return Hcr(l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_HCR));
  }

  /// write GICV_HCR / ICH_HCR_EL2
  void write_hcr(Vmm::Arm::Gic_h::Hcr hcr) const
  { l4_vcpu_e_write_32(*_vcpu, L4_VCPU_E_GIC_HCR, hcr.raw); }

  /// read GICV_MISR / ICH_MISR_EL2
  Vmm::Arm::Gic_h::Misr misr() const
  {
    using Vmm::Arm::Gic_h::Misr;
    return Misr(l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_MISR));
  }

  /// read GICH_VTR / ICH_VTR_EL2
  Vmm::Arm::Gic_h::Vtr vtr() const
  {
    using Vmm::Arm::Gic_h::Vtr;
    return Vtr(l4_vcpu_e_read_32(*_vcpu, L4_VCPU_E_GIC_VTR));
  }

  /**
   * Get the affinity from the vCPU MPIDR.
   * \pre the CPU interface must be initialized (see initialize()).
   */
  l4_uint32_t affinity() const
  {
    l4_uint64_t mpidr = l4_vcpu_e_read(*_vcpu, L4_VCPU_E_VMPIDR);
    return (mpidr & 0x00ffffff) | ((mpidr >> 8) & 0xff000000);
  }

  void register_doorbell(Vmm::Generic_cpu_dev *cpu)
  {
    if (!cpu->registry()->register_irq_obj(&_doorbell_irq))
      Fatal().abort("attach doorbell interrupt");
    if (l4_error(thread_cap()->register_doorbell_irq(_doorbell_irq.obj_cap())) < 0)
      Fatal().abort("install doorbell interrupt");
  }

private:
  /// SGI and PPI IRQ array
  Ppi_irq_array _local_irq;

  /// SPI IRQ array from distributor
  Spi_irq_array *_spis;

  /// The associated vCPU
  Vmm::Vcpu_ptr _vcpu = Vmm::Vcpu_ptr(nullptr);

  /// The direct IRQ injection doorbell IRQ
  Irq_sentinel _doorbell_irq;

  void _set_elsr(l4_uint32_t bits) const
  {
    unsigned id = L4_VCPU_E_GIC_ELSR;
    l4_uint32_t e = l4_vcpu_e_read_32(*_vcpu, id);
    l4_vcpu_e_write_32(*_vcpu, id, e | bits);
  }

  void _clear_elsr(l4_uint32_t bits) const
  {
    unsigned id = L4_VCPU_E_GIC_ELSR;
    l4_uint32_t e = l4_vcpu_e_read_32(*_vcpu, id);
    l4_vcpu_e_write_32(*_vcpu, id, e & ~bits);
  }

  bool queue_and_notify(Irq *irq)
  {
    queue(irq);
    return true;
  }
};

class Dist_v3
: public Ic,
  public Vmm::Mmio_device_t<Dist_v3>
{
  enum { Gicd_ctlr_must_set = 5UL << 4 }; // DS, ARE

  l4_uint32_t _ctlr;

public:
  using Irq = Cpu::Irq;
  using Const_irq = Cpu::Const_irq;

  enum Regs
  {
    CTLR  = 0x000,
    TYPER = 0x004, // RO
    IIDR  = 0x008, // RO
    SGIR  = 0xf00, // WO
  };

  Dist_v3()
  : _ctlr(Gicd_ctlr_must_set),
    _cpu(&_spis),
    _redist(this),
    _sgir(this)
  {}

  void reinit()
  {
    _cpu.local_irqs().reinit();
    _spis.reinit();
  }

  Irq &ppi(unsigned ppi)
  {
    return _cpu.local_irqs()[ppi];
  }

  Const_irq &ppi(unsigned ppi) const
  {
    return _cpu.local_irqs()[ppi];
  }

  Irq &spi(unsigned spi, bool alloc = false)
  {
    assert (spi < _spis.size());
    if (alloc)
      return _spis.alloc(spi);
    else
      return _spis[spi];
  }

  Const_irq &spi(unsigned spi) const
  {
    assert (spi < _spis.size());
    return _spis[spi];
  }

  void clear(unsigned) override {}

  void bind_eoi_handler(unsigned irq, Eoi_handler *handler) override
  {
    Irq &pin = spi(irq - Num_local, true);

    if (handler && pin.get_eoi_handler())
      Fatal().abort("Assigning EOI handler to GIC");

    pin.set_eoi(handler);
  }

  Eoi_handler *get_eoi_handler(unsigned irq) const override
  { return spi(irq - Num_local).get_eoi_handler(); }

  void bind_virq_handler(unsigned irq, Virq_handler *handler) override
  {
    auto &pin = spi(irq - Num_local, true);

    if (handler && pin.get_eoi_handler())
      Fatal().abort("Assigning EOI handler to GIC");

    pin.set_virq(handler);
    pin.reconfigure();
  }

  void bind_cpulocal_virq_handler(unsigned irq, Virq_handler *handler) override
  {
    auto &pin = ppi(irq);

    if (handler && pin.get_eoi_handler())
      Fatal().abort("Assigning EOI handler to GIC");

    pin.set_virq(handler);
    pin.reconfigure();
  }

  /// write to the GICD_CTLR.
  void write_ctlr(l4_uint32_t val)
  {
    _ctlr = (val & 3U) | Gicd_ctlr_must_set;
  }

  /// read to the GICD_TYPER.
  l4_uint32_t get_typer() const
  {
    // CPUNumber: ARE always enabled, see also GICD_CTLR.
    // No1N:      1 of N SPI routing model not supported
    l4_uint32_t type = ((Num_local + Num_spis) / 32 - 1)
                       | (0 << 5)
                       | (1 << 25);

    // IDBits: 10 (IDs 0-1019, 1020-1023 are reserved).
    type |= (9 << 19);

    return type;
  }

  /// read to the CoreSight IIDRs.
  l4_uint32_t iidr_read(unsigned r) const
  {
    if (r == 0x18)
      return 3 << 4; // GICv3

    return 0;
  }

  void inject_irq(Irq &irq)
  {
    if (irq.pending(true, &_cpu))
      _cpu.inject(irq);
  }

  void set(unsigned irq) override
  {
    if (irq < Num_local)
      inject_irq(_cpu.local_irq(irq));
    else
      inject_irq(spi(irq - Num_local));
  }

  bool schedule_irqs()
  {
    _cpu.handle_eois();

    unsigned pmask = this->_prio_mask;

    for (;;)
      {
        unsigned empty_lr = _cpu.get_empty_lr();

        if (!empty_lr)
          return true;

        Irq *irq = _cpu.take_pending_irq(pmask);
        if (!irq)
          return _cpu.pending_irqs();

        if (0)
          printf("Inject: irq=%u... ", irq->id());
        _cpu.add_pending_irq(empty_lr - 1, *irq);
      }
  }

  void handle_maintenance_irq()
  {
    auto misr = _cpu.misr();
    auto hcr = _cpu.hcr();
    if (misr.grp0_e())
      {
        hcr.vgrp0_eie() = 0;
        hcr.vgrp0_die() = 1;
      }

    if (misr.grp0_d())
      {
        hcr.vgrp0_eie() = 1;
        hcr.vgrp0_die() = 0;
      }

    if (misr.grp1_e())
      {
        hcr.vgrp1_eie() = 0;
        hcr.vgrp1_die() = 1;
      }

    if (misr.grp1_d())
      {
        hcr.vgrp1_eie() = 1;
        hcr.vgrp1_die() = 0;
      }

    _cpu.write_hcr(hcr);
    _cpu.handle_maintenance_irq();
  }

  class Redist : public Vmm::Mmio_device_t<Redist>
  {
  private:
    Dist_v3 *_dist;

    enum
    {
      IID  = 0x43b,
      IID2 = 3 << 4,
      // All but proc_num and affinity and last.
      // CommonLPIAff = 0 -> All redistributors must share LPI config table.
      // DirectLPI = 0 -> Direct injection of LPIs not supported.
      TYPE = 0,
    };

    l4_uint32_t status() const
    { return 0; }

    void status(l4_uint32_t) const
    {}

    l4_uint32_t type() const
    {
      l4_uint32_t type = TYPE;
      return type;
    }

    enum
    {
      CTLR      = 0x0,
      IIDR      = 0x4,
      TYPER     = 0x8,
      STATUSR   = 0x10,
      WAKER     = 0x14,
      PROPBASER = 0x70,
      PENDBASER = 0x78,
      IIDR2     = 0xffe8,
    };

    l4_uint64_t read_rd(Cpu *cif, unsigned reg, char size, bool last)
    {
      unsigned r32 = reg & ~3u;
      using Ma = Vmm::Mem_access;

      switch (r32)
        {
        case CTLR:
          return 0;

        case IIDR:
          return IID;

        case IIDR2:
          return IID2;

        case TYPER:
        case TYPER + 4:
          return Ma::read(type() | cif->get_typer() | (last ? 0x10 : 0x00),
                          reg, size);
        case STATUSR:
          return status();

        case WAKER:
          return 0;

        default:
          break;
        }

      return 0;
    }

    void write_rd(Cpu *, unsigned reg, char /*size*/, l4_uint64_t value)
    {
      unsigned r32 = reg & ~3u;

      switch (r32)
        {
        case STATUSR:
          status(value);
          return;

        case WAKER:
          return;

        default:
          break;
        }
      return;
    }

  public:
    enum
    {
      Stride = 17, // 17bit stride -> 2 64K regions RD + SGI
    };

    explicit Redist(Dist_v3 *dist)
    : _dist(dist)
    {
    }

    l4_uint64_t read(unsigned reg, char size)
    {
      unsigned cpu_id = reg >> Stride;
      if (cpu_id > 0)
        return 0;

      unsigned blk = (reg >> 16) & ~((~0u) << (Stride - 16));
      reg &= 0xffff;

      l4_uint64_t res = 0;
      switch (blk)
        {
          case 0:
            return read_rd(&_dist->_cpu, reg, size, true);
          case 1:
            _dist->read_multi_irq(reg, size, &res);
            return res;
          default:
            return 0;
        }
    }

    void write(unsigned reg, char size, l4_uint64_t value)
    {
      unsigned cpu_id = reg >> Stride;
      if (cpu_id > 0)
        return;

      unsigned blk = (reg >> 16) & ~((~0u) << (Stride - 16));
      reg &= 0xffff;
      switch (blk)
        {
          case 0:
            return write_rd(&_dist->_cpu, reg, size, value);
          case 1:
            _dist->write_multi_irq(reg, size, value);
            return;
          default:
            break;
        }
    }
  };

  class Sgir_sysreg : public Vmm::Arm::Sys_reg
  {
  private:
    void sgi_tgt(unsigned intid, l4_uint64_t target)
    {
      unsigned const aff = ((target >> 16) & 0xff)
                           | ((target >> 24) & 0xff00)
                           | ((target >> 32) & 0xff0000);

      unsigned const tgtlist = target & 0xffff;

      unsigned a = dist->_cpu.affinity();
      if ((a >> 8) != aff || ((a & 0xff) > 0xf))
        return;

      if (!((1u << (a & 0xf)) & tgtlist))
        return;

      dist->inject_irq(dist->_cpu.local_irq(intid));
    }

  public:
    Dist_v3 *dist;

    explicit Sgir_sysreg(Dist_v3 *d) : dist(d) {}

    l4_uint64_t read(Vmm::Vcpu_ptr, Key) override
    { return 0; }

    void write(Vmm::Vcpu_ptr, Key, l4_uint64_t val) override
    {
      unsigned const intid = (val >> 24) & 0xf;

      if (! (val & (1ull << 40)))
        sgi_tgt(intid, val);
    }
  };

  void setup_cpu(Vmm::Generic_cpu_dev *cpu)
  {
    _cpu.thread_cap(cpu->thread_cap());
    _cpu.vcpu(cpu->vcpu());
    _cpu.register_doorbell(cpu);
    _prio_mask = ~((1U << (7 - _cpu.vtr().pri_bits())) - 1U);

    if (_prio_mapper)
      _cpu.setup_prio_cfg(_prio_mapper->mult, _prio_mapper->lower);
  }

  void setup_gic(Vmm::Guest *vmm);

private:
  /// \group Per IRQ register interfaces
  /// \{
  enum Reg_group_idx
  {
    R_group = 0,
    R_isenable,
    R_icenable,
    R_ispend,
    R_icpend,
    R_isactive,
    R_icactive,
    R_prio,
    R_target,
    R_cfg,
    R_grpmod,
    R_nsacr,
    R_route
  };

  l4_uint32_t irq_mmio_read(Const_irq const &irq, unsigned rgroup)
  {
    switch (rgroup)
      {
      case R_group:    return irq.group();
      case R_isenable:
      case R_icenable: return irq.enabled();
      case R_ispend:
      case R_icpend:   return irq.pending();
      case R_isactive:
      case R_icactive: return irq.active();
      case R_prio:     return irq.prio();
      case R_target:   return 0;
      case R_cfg:      return irq.config();
      case R_grpmod:   return 0;
      case R_nsacr:    return 0;
      default:         assert (false); return 0;
      }
  }

  void irq_mmio_write(Irq &irq, unsigned rgroup, l4_uint32_t value)
  {
    switch (rgroup)
      {
      case R_group:    irq.group(value);                      return;
      case R_isenable: if (value) irq.enable(true, &_cpu);    return;
      case R_icenable: if (value) irq.enable(false, &_cpu);   return;
      case R_ispend:   if (value) irq.pending(true, &_cpu);   return;
      case R_icpend:   if (value) irq.pending(false, &_cpu);  return;
      case R_isactive:                                 return;
      case R_icactive:                                 return;
      case R_prio:     irq.prio(value & _prio_mask, _prio_mapper); return;
      case R_target:                                   return;
      case R_cfg:      irq.config(value);              return;
      case R_grpmod:   /* GICD_CTRL.DS=1 -> RAZ/WI */  return;
      case R_nsacr:    /* GICD_CTRL.DS=1 -> RAZ/WI */  return;
      default:         assert (false);                 return;
      }
  }
  /// \} end of per IRQ registers

  /**
   * Helper to demux multiple IRQs-per register accesses.
   * \note Local IRQs vs SPIs must be resolved already.
   */
  template<unsigned SHIFT, typename ARR, typename OP>
  void _demux_irq_reg(ARR &irqs,
                      unsigned s, unsigned n,
                      unsigned reg, OP &&op)
  {
    unsigned const rshift = 8 >> SHIFT;
    l4_uint32_t const mask = 0xff >> (8 - rshift);
    for (unsigned x = 0; x < n; ++x)
      {
        unsigned const i = x + s;
        op(irqs[i], reg, mask, rshift * x);
      }
  }

  /**
   * Helper to demux multiple IRQs-per register accesses.
   * \note Local IRQs vs SPIs must be resolved already.
   */
  template<unsigned SHIFT, typename OP>
  void _demux_irq_reg(unsigned reg, unsigned offset,
                      unsigned size,
                      OP &&op)
  {
    unsigned const irq_s = (offset & (~0U) << size) << SHIFT;
    unsigned const nirq = (1 << size) << SHIFT;

    if (irq_s < Num_local)
      _demux_irq_reg<SHIFT>(_cpu.local_irqs(), irq_s, nirq, reg, op);
    else if (irq_s - Num_local < _spis.size())
      _demux_irq_reg<SHIFT>(_spis, irq_s - Num_local, nirq, reg, op);
  }

  /**
   * Helper to demux a complete range of multi IRQ registers with
   * equal number of IRQs per register (given by SHIFT).
   * \pre `reg` >= `START`
   * \retval false if `reg` >= END
   * \retval true if `reg` < END;
   */
  template<unsigned BLK, unsigned START, unsigned END,
           unsigned SHIFT, typename OP>
  bool _demux_irq_block(unsigned reg, unsigned size, OP &&op)
  {
    unsigned const rsh = 10 - SHIFT;
    static_assert((START & ((1U << rsh) - 1)) == 0U, "low bits of START zero");
    static_assert((END   & ((1U << rsh) - 1)) == 0U, "low bits of END zero");
    if (reg < END)
      {
        unsigned const x = reg >> rsh;
        _demux_irq_reg<SHIFT>(x - (START >> rsh) + BLK,
                              reg & ~((~0U) << rsh), size, op);
        return true;
      }
    return false;
  }

  /**
   * Demux the access to the whole multi-IRQ register range of the
   * GIC distributor.
   */
  template<typename OP>
  bool _demux_per_irq(unsigned reg, unsigned size, OP &&op)
  {
    if (reg < 0x80)
      return false;

    if (_demux_irq_block<R_group, 0x80, 0x400, 3>(reg, size, op))
      return true;

    if (_demux_irq_block<R_prio, 0x400, 0xc00, 0>(reg, size, op))
      return true;

    if (_demux_irq_block<R_cfg,  0xc00, 0xe00, 2>(reg, size, op))
      return true;

    if (_demux_irq_block<R_grpmod, 0xd00, 0xd80, 3>(reg, size, op))
      return true;

    if (_demux_irq_block<R_nsacr, 0xe00, 0xf00, 2>(reg, size, op))
      return true;

    return false;
  }

  /**
   * Helper to access the IIDR register range of CoreSight GICs
   * This helper forwards to the iidr_read interface.
   * \retval true if `reg` is in the IIDR range of the device.
   * \retval false otherwise
   */
  bool _iidr_try_read(unsigned reg, char size, l4_uint64_t *val)
  {
    if (size == 2 && reg >= 0xffd0 && reg <= 0xfffc)
      {
        *val = iidr_read(reg - 0xffd0);
        return true;
      }

    return false;
  }

  /**
   * Helper for reads in the GICD header area 0x00 - 0x10
   */
  l4_uint32_t _read_gicd_header(unsigned reg)
  {
    unsigned r = reg >> 2;
    switch (r)
      {
      case 0: return _ctlr;       // GICD_CTRL
      case 1: return get_typer(); // GICD_TYPER
      case 2: return 0x43b;       // GICD_IIDR
      default: break;             // includes GICD_TYPER2
      }
    return 0;
  }

protected:

  /**
   * Read a register in the multi IRQs register range of GICD.
   * \retval true  if `reg` is handled by the function.
   * \retval false otherwise.
   */
  bool
  read_multi_irq(unsigned reg, char size, l4_uint64_t *res)
  {
    auto rd = [this,res](Const_irq const &irq, unsigned r, l4_uint32_t mask,
                        unsigned shift)
      {
        *res |= (this->irq_mmio_read(irq, r) & mask) << shift;
      };

    return _demux_per_irq(reg, size, rd);
  }

  /**
   * Write a register in the multi IRQs register range of GICD.
   * \retval true  if `reg` is handled by the function.
   * \retval false otherwise.
   */
  bool
  write_multi_irq(unsigned reg, char size, l4_uint32_t value)
  {
    auto wr = [this,value](Irq &irq, unsigned r, l4_uint32_t mask,
                           unsigned shift)
      {
        this->irq_mmio_write(irq, r, (value >> shift) & mask);
      };

    return _demux_per_irq(reg, size, wr);
  }

public:
  l4_uint64_t read(unsigned reg, char size)
  {
    if (reg < 0x10) // GICD_CTRL..GICD_TYPER2
      return _read_gicd_header(reg);

    if (reg == 0x10) // GICD_STATUS
      return 0;

    if (reg < 0x80) // < GICD_IGROUPR
      return 0;

    l4_uint64_t res = 0;
    if (read_multi_irq(reg, size, &res))
      return res;

    if (_iidr_try_read(reg, size, &res))
      return res;

    return 0;
  }

  void write(unsigned reg, char size, l4_uint32_t value)
  {
    if (reg == 0 && size == 2)
      {
        write_ctlr(value);
        return;
      }

    if (reg < 0x80) // < GICD_IGROUPR
      return; // all RO, WI, WO or not implemented

    write_multi_irq(reg, size, value);
  }

  void set_irq_priority_range(unsigned min, unsigned max)
  {
    _prio_range_mapper.set_irq_priority_range(min, max);
    _prio_mapper = &_prio_range_mapper;
  }

protected:
  Spi_irq_array _spis;
  Cpu _cpu;
  l4_uint8_t _prio_mask;

  Redist _redist;
  Sgir_sysreg _sgir;


  Prio_mapper *_prio_mapper = nullptr;
  Prio_mapper _prio_range_mapper;
};

}

