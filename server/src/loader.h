/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Kernkonzept GmbH.
 * Author(s): Jan Klötzke <jan.kloetzke@kernkonzept.com>
 */

#pragma once

#include <l4/util/elf.h>
#include <l4/sys/vm>

namespace Loader {

class Elf_phdr
{
private:
  void const *_hdr;
  bool _64;

public:
  Elf_phdr(void const *hdr, bool _64) : _hdr(hdr), _64(_64) {}
  Elf32_Phdr const *hdr32() const { return (Elf32_Phdr const*)(_hdr); }
  Elf64_Phdr const *hdr64() const { return (Elf64_Phdr const*)(_hdr); }

  char const *phdr_type() const;
  unsigned long type() const { return _64?hdr64()->p_type:hdr32()->p_type; }
  unsigned long paddr() const { return _64?hdr64()->p_paddr:hdr32()->p_paddr; }
  unsigned long vaddr() const { return _64?hdr64()->p_vaddr:hdr32()->p_vaddr; }
  unsigned long memsz() const { return _64?hdr64()->p_memsz:hdr32()->p_memsz; }
  unsigned long filesz() const
  { return _64?hdr64()->p_filesz:hdr32()->p_filesz; }
  unsigned long flags() const { return _64?hdr64()->p_flags:hdr32()->p_flags; }
  unsigned long offset() const
  { return _64?hdr64()->p_offset:hdr32()->p_offset; }
  unsigned long align() const { return _64?hdr64()->p_align:hdr32()->p_align; }
};

class Elf_ehdr
{
private:
  char e_ident[16];
  unsigned short e_type;
  unsigned short e_machine;
  unsigned e_version;
public:
  template< typename T >
  T element(unsigned long offset) const
  { return reinterpret_cast<T>((unsigned long)this + offset); }

  bool is_valid() const
  {
    return    l4util_elf_check_magic((ElfW(Ehdr) *)this)
           && l4util_elf_check_arch((ElfW(Ehdr) *)this);
  }

  bool is_64() const
  {
    return e_ident[EI_CLASS] == ELFCLASS64;
  }

private:
  Elf64_Ehdr const *hdr64() const { return (Elf64_Ehdr*)this; }
  Elf32_Ehdr const *hdr32() const { return (Elf32_Ehdr*)this; }

public:

  bool is_dynamic() const
  {
    if (is_64())
      return hdr64()->e_type == ET_DYN;
    else
      return hdr32()->e_type == ET_DYN;
  }

  l4_addr_t phdrs_offset() const
  {
    if (is_64())
      return hdr64()->e_phoff;
    else
      return hdr32()->e_phoff;
  }

  l4_size_t phdr_size() const
  {
    if (is_64())
      return hdr64()->e_phentsize;
    else
      return hdr32()->e_phentsize;
  }

  unsigned num_phdrs() const
  {
    if (is_64())
      return hdr64()->e_phnum;
    else
      return hdr32()->e_phnum;
  }

  unsigned long entry() const
  {
    if (is_64())
      return hdr64()->e_entry;
    else
      return hdr32()->e_entry;
  }
};

class Elf_binary
{
public:
  Elf_binary() = default;

  explicit Elf_binary(void const *data)
  {
    auto *eh = reinterpret_cast<Elf_ehdr const*>(data);
    if (!eh->is_valid())
      return;

    _eh = eh;
  }

  bool is_valid() const
  {
    return _eh;
  }

  bool is_64() const
  {
    return _eh->is_64();
  }

  unsigned long entry() const
  {
    return _eh->entry();
  }

  unsigned num_phdrs() const
  {
    return _eh->num_phdrs();
  }

  Elf_phdr phdr(int index) const
  {
    auto *ph = reinterpret_cast<char const *>(_eh) + _eh->phdrs_offset();
    return Elf_phdr(ph + index * _eh->phdr_size(), is_64());
  }

  template< typename F >
  void iterate_phdr(F const &func) const
  {
    unsigned n = num_phdrs();
    for (unsigned i = 0; i < n; ++i)
      func(phdr(i), _eh);
  }

private:
  Elf_ehdr const *_eh = nullptr;
};

}
