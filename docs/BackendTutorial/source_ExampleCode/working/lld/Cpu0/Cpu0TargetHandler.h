//===- lib/ReaderWriter/ELF/Cpu0/Cpu0TargetHandler.h ------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_ELF_Cpu0_TARGET_HANDLER_H
#define LLD_READER_WRITER_ELF_Cpu0_TARGET_HANDLER_H

#include "DefaultTargetHandler.h"
#include "Cpu0RelocationHandler.h"
#include "TargetLayout.h"

#include "lld/ReaderWriter/Simple.h"

#include "lld/Core/Atom.h"

struct Cpu0AtomAddress {
  const lld::Atom* PAtom;
  uint64_t Addr;
};

extern Cpu0AtomAddress Cpu0AtomAddr[100];
extern int Cpu0AtomAddrSize;

extern uint64_t textSectionAddr;

namespace lld {
namespace elf {
typedef llvm::object::ELFType<llvm::support::big, 4, false> Cpu0ELFType;
class Cpu0LinkingContext;

class Cpu0TargetHandler LLVM_FINAL
    : public DefaultTargetHandler<Cpu0ELFType> {
public:
  Cpu0TargetHandler(Cpu0LinkingContext &targetInfo);

  virtual TargetLayout<Cpu0ELFType> &targetLayout() {
    return _targetLayout;
  }

  virtual const Cpu0TargetRelocationHandler &getRelocationHandler() const {
    return _relocationHandler;
  }

  virtual void addFiles(InputFiles &f);
#if 0
  void finalizeSymbolValues() {
    if (_context.isDynamic()) {
      auto gottextSection = _targetLayout.findOutputSection(".text");
      if (gottextSection)
        textSectionAddr = gottextSection->virtualAddr();
      else
        textSectionAddr = 0;
      uint64_t sectionstartaddr = 0;
      uint64_t startaddr = 0;
      uint64_t sectionsize = 0;
      bool isFirstSection = true;
      for (auto si : gottextSection->sections()) {
        if (isFirstSection) {
          startaddr = si->virtualAddr();
          isFirstSection = false;
        }
        sectionstartaddr = si->virtualAddr();
        sectionsize = si->memSize();
      }
    }
  }
#endif

private:
  class GOTFile : public SimpleFile {
  public:
    GOTFile(const ELFLinkingContext &eti) : SimpleFile(eti, "GOTFile") {}
    llvm::BumpPtrAllocator _alloc;
  } _gotFile;

  Cpu0TargetRelocationHandler _relocationHandler;
  TargetLayout<Cpu0ELFType> _targetLayout;
};
} // end namespace elf
} // end namespace lld

#endif
