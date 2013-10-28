//===- lib/ReaderWriter/ELF/Cpu0/Cpu0LinkingContext.cpp -------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Atoms.h"
#include "Cpu0LinkingContext.h"

#include "lld/Core/File.h"
#include "lld/Core/Instrumentation.h"
#include "lld/Core/Parallel.h"
#include "lld/Core/Pass.h"
#include "lld/Core/PassManager.h"
#include "lld/ReaderWriter/Simple.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringSwitch.h"

using namespace lld;

using namespace lld::elf;

namespace {
using namespace llvm::ELF;

// .plt value (entry 0)
const uint8_t cpu0BootAtomContent[16] = {
  0x36, 0xff, 0xff, 0xfc, // jmp _start
  0x36, 0x00, 0x00, 0x04, // jmp 4
  0x36, 0x00, 0x00, 0x04, // jmp 4
  0x36, 0xff, 0xff, 0xfc // jmp -4
};

#ifdef DYNLINKER
// .got values
const uint8_t cpu0GotAtomContent[16] = { 0 };

// .plt value (entry 0)
const uint8_t cpu0Plt0AtomContent[16] = {
  0x02, 0xea, 0x00, 0x04, // st $lr, $zero, reloc-index ($gp)
  0x02, 0xba, 0x00, 0x08, // st $fp, $zero, reloc-index ($gp)
  0x02, 0xda, 0x00, 0x0c, // st $sp, $zero, reloc-index ($gp)
  0x36, 0xff, 0xff, 0xfc  // jmp dynamic_linker
};

// .plt values (other entries)
const uint8_t cpu0PltAtomContent[16] = {
  0x09, 0x60, 0x00, 0x00, // addiu $t9, $zero, reloc-index (=.dynsym_index)
  0x02, 0x6a, 0x00, 0x00, // st $t9, $zero, reloc-index ($gp)
  0x01, 0x6a, 0x00, 0x10, // ld $t9, 0x10($gp) (0x10($gp) point to plt0
  0x3c, 0x60, 0x00, 0x00  // ret $t9 // jump to Cpu0.Stub
};
#endif // DYNLINKER

/// boot record
class Cpu0BootAtom : public PLT0Atom {
public:
  Cpu0BootAtom(const File &f) : PLT0Atom(f) {
#ifndef NDEBUG
    _name = ".PLT0";
#endif
  }
  virtual ArrayRef<uint8_t> rawContent() const {
    return ArrayRef<uint8_t>(cpu0BootAtomContent, 16);
  }
};

#ifdef DYNLINKER
/// \brief Atoms that are used by Cpu0 dynamic linking
class Cpu0GOTAtom : public GOTAtom {
public:
  Cpu0GOTAtom(const File &f, StringRef secName) : GOTAtom(f, secName) {}

  virtual ArrayRef<uint8_t> rawContent() const {
    return ArrayRef<uint8_t>(cpu0GotAtomContent, 16);
  }
};

class Cpu0PLT0Atom : public PLT0Atom {
public:
  Cpu0PLT0Atom(const File &f) : PLT0Atom(f) {
#ifndef NDEBUG
    _name = ".PLT0";
#endif
  }
  virtual ArrayRef<uint8_t> rawContent() const {
    return ArrayRef<uint8_t>(cpu0Plt0AtomContent, 16);
  }
};

class Cpu0PLTAtom : public PLTAtom {
public:
  Cpu0PLTAtom(const File &f, StringRef secName) : PLTAtom(f, secName) {}

  virtual ArrayRef<uint8_t> rawContent() const {
    return ArrayRef<uint8_t>(cpu0PltAtomContent, 16);
  }
};
#endif // DYNLINKER

class ELFPassFile : public SimpleFile {
public:
  ELFPassFile(const ELFLinkingContext &eti) : SimpleFile(eti, "ELFPassFile") {}

  llvm::BumpPtrAllocator _alloc;
};

/// \brief Create GOT and PLT entries for relocations. Handles standard GOT/PLT
/// along with IFUNC and TLS.
template <class Derived> class GOTPLTPass : public Pass {
  /// \brief Handle a specific reference.
  void handleReference(const DefinedAtom &atom, const Reference &ref) {
    switch (ref.kind()) {

    case R_CPU0_CALL16:
      static_cast<Derived *>(this)->handlePLT32(ref);
      break;

    case R_CPU0_PC24:
      static_cast<Derived *>(this)->handlePC24(ref);
      break;
#if 0
    case R_CPU0_GOTTPOFF: // GOT Thread Pointer Offset
      static_cast<Derived *>(this)->handleGOTTPOFF(ref);
      break;
    case R_CPU0_GOTPCREL:
      static_cast<Derived *>(this)->handleGOTPCREL(ref);
      break;
#endif
    }
  }

protected:
#ifdef DYNLINKER
  /// \brief get the PLT entry for a given IFUNC Atom.
  ///
  /// If the entry does not exist. Both the GOT and PLT entry is created.
  const PLTAtom *getIFUNCPLTEntry(const DefinedAtom *da) {
    auto plt = _pltMap.find(da);
    if (plt != _pltMap.end())
      return plt->second;
    auto ga = new (_file._alloc) Cpu0GOTAtom(_file, ".got.plt");
    ga->addReference(R_CPU0_RELGOT, 0, da, 0);
    auto pa = new (_file._alloc) Cpu0PLTAtom(_file, ".plt");
    pa->addReference(R_CPU0_PC24, 2, ga, -4);
#ifndef NDEBUG
    ga->_name = "__got_ifunc_";
    ga->_name += da->name();
    pa->_name = "__plt_ifunc_";
    pa->_name += da->name();
#endif
    _gotMap[da] = ga;
    _pltMap[da] = pa;
    _gotVector.push_back(ga);
    _pltVector.push_back(pa);
    return pa;
  }
#endif // DYNLINKER

  /// \brief Redirect the call to the PLT stub for the target IFUNC.
  ///
  /// This create a PLT and GOT entry for the IFUNC if one does not exist. The
  /// GOT entry and a RELGOT relocation to the original target resolver.
  ErrorOr<void> handleIFUNC(const Reference &ref) {
    auto target = dyn_cast_or_null<const DefinedAtom>(ref.target());
#ifdef DYNLINKER
    if (target && target->contentType() == DefinedAtom::typeResolver)
      const_cast<Reference &>(ref).setTarget(getIFUNCPLTEntry(target));
#endif // DYNLINKER
    return error_code::success();
  }

#ifdef DYNLINKER
  /// \brief Create a GOT entry for the TP offset of a TLS atom.
  const GOTAtom *getGOTTPOFF(const Atom *atom) {
    auto got = _gotMap.find(atom);
    if (got == _gotMap.end()) {
      auto g = new (_file._alloc) Cpu0GOTAtom(_file, ".got");
      g->addReference(R_CPU0_TLS_TPREL32, 0, atom, 0);
#ifndef NDEBUG
      g->_name = "__got_tls_";
      g->_name += atom->name();
#endif
      _gotMap[atom] = g;
      _gotVector.push_back(g);
      return g;
    }
    return got->second;
  }

  /// \brief Create a TLS_TPREL32 GOT entry and change the relocation to a PC24 to
  /// the GOT.
  void handleGOTTPOFF(const Reference &ref) {
    const_cast<Reference &>(ref).setTarget(getGOTTPOFF(ref.target()));
    const_cast<Reference &>(ref).setKind(R_CPU0_PC24);
  }

  /// \brief Create a GOT entry containing 0.
  const GOTAtom *getNullGOT() {
    if (!_null) {
      _null = new (_file._alloc) Cpu0GOTAtom(_file, ".got.plt");
#ifndef NDEBUG
      _null->_name = "__got_null";
#endif
    }
    return _null;
  }

  const GOTAtom *getGOT(const DefinedAtom *da) {
    auto got = _gotMap.find(da);
    if (got == _gotMap.end()) {
      auto g = new (_file._alloc) Cpu0GOTAtom(_file, ".got");
      g->addReference(R_CPU0_32, 0, da, 0);
#ifndef NDEBUG
      g->_name = "__got_";
      g->_name += da->name();
#endif
      _gotMap[da] = g;
      _gotVector.push_back(g);
      return g;
    }
    return got->second;
  }

  /// \brief Handle a GOTPCREL relocation to an undefined weak atom by using a
  /// null GOT entry.
  void handleGOTPCREL(const Reference &ref) {
    const_cast<Reference &>(ref).setKind(R_CPU0_PC24);
    if (isa<UndefinedAtom>(ref.target()))
      const_cast<Reference &>(ref).setTarget(getNullGOT());
    else if (const DefinedAtom *da = dyn_cast<const DefinedAtom>(ref.target()))
      const_cast<Reference &>(ref).setTarget(getGOT(da));
  }
#endif // DYNLINKER

public:
  GOTPLTPass(const ELFLinkingContext &ti, bool isExe)
      : _file(ti), _null(nullptr), _PLT0(nullptr), _got0(nullptr)/*,
        _got1(nullptr)*/, _boot(new Cpu0BootAtom(_file)), _isStaticExe(isExe) 
       { }

  /// \brief Do the pass.
  ///
  /// The goal here is to first process each reference individually. Each call
  /// to handleReference may modify the reference itself and/or create new
  /// atoms which must be stored in one of the maps below.
  ///
  /// After all references are handled, the atoms created during that are all
  /// added to mf.
  virtual void perform(MutableFile &mf) {
    ScopedTask task(getDefaultDomain(), "Cpu0 GOT/PLT Pass");
    // Process all references.
    for (const auto &atom : mf.defined())
      for (const auto &ref : *atom)
        handleReference(*atom, *ref);

    // Add all created atoms to the link.
    uint64_t ordinal = 0;
    if (_isStaticExe) {
      MutableFile::DefinedAtomRange atomRange = mf.definedAtoms();
      auto it = atomRange.begin();
      bool find = false;
      for (it = atomRange.begin(); it < atomRange.end(); it++) {
        if ((*it)->name() == "_Z5startv") {
          find = true;
          break;
        }
      }
      assert(find && "not found _Z5startv\n");
      _boot->addReference(R_CPU0_PC24, 0, *it, -3);
      _boot->setOrdinal(ordinal++);
      mf.addAtom(*_boot);
    }
#ifdef DYNLINKER
    if (_PLT0) {
      MutableFile::DefinedAtomRange atomRange = mf.definedAtoms();
      auto it = atomRange.begin();
      bool find = false;
      for (it = atomRange.begin(); it < atomRange.end(); it++) {
        if ((*it)->name() == "_Z14dynamic_linkerv") {
          find = true;
          break;
        }
      }
      assert(find && "Cannot find _Z14dynamic_linkerv()");
      _PLT0->addReference(R_CPU0_PC24, 12, *it, -3);
      _PLT0->setOrdinal(ordinal++);
      mf.addAtom(*_PLT0);
    }
    for (auto &plt : _pltVector) {
      plt->setOrdinal(ordinal++);
      mf.addAtom(*plt);
    }
    if (_null) {
      _null->setOrdinal(ordinal++);
      mf.addAtom(*_null);
    }
    if (_PLT0) {
      _got0->setOrdinal(ordinal++);
      mf.addAtom(*_got0);
    }
    for (auto &got : _gotVector) {
      got->setOrdinal(ordinal++);
      mf.addAtom(*got);
    }
#endif // DYNLINKER
  }

protected:
  /// \brief Owner of all the Atoms created by this pass.
  ELFPassFile _file;

  /// \brief Map Atoms to their GOT entries.
  llvm::DenseMap<const Atom *, GOTAtom *> _gotMap;

  /// \brief Map Atoms to their PLT entries.
  llvm::DenseMap<const Atom *, PLTAtom *> _pltMap;

  /// \brief the list of GOT/PLT atoms
  std::vector<GOTAtom *> _gotVector;
  std::vector<PLTAtom *> _pltVector;

  PLT0Atom *_boot;

  /// \brief GOT entry that is always 0. Used for undefined weaks.
  GOTAtom *_null;

  /// \brief The got and plt entries for .PLT0. This is used to call into the
  /// dynamic linker for symbol resolution.
  /// @{
  PLT0Atom *_PLT0;
  GOTAtom *_got0;
public:
  bool _isStaticExe;
  /// @}
};

/// This implements the static relocation model. Meaning GOT and PLT entries are
/// not created for references that can be directly resolved. These are
/// converted to a direct relocation. For entries that do require a GOT or PLT
/// entry, that entry is statically bound.
///
/// TLS always assumes module 1 and attempts to remove indirection.
class StaticGOTPLTPass LLVM_FINAL : public GOTPLTPass<StaticGOTPLTPass> {
public:
  StaticGOTPLTPass(const elf::Cpu0LinkingContext &ti, bool isExe)
      : GOTPLTPass(ti, isExe) { }

  ErrorOr<void> handlePLT32(const Reference &ref) {
    // __tls_get_addr is handled elsewhere.
    if (ref.target() && ref.target()->name() == "__tls_get_addr") {
      const_cast<Reference &>(ref).setKind(R_CPU0_NONE);
      return error_code::success();
    } else
      // Static code doesn't need PLTs.
      const_cast<Reference &>(ref).setKind(R_CPU0_PC24);
    // Handle IFUNC.
    if (const DefinedAtom *da =
            dyn_cast_or_null<const DefinedAtom>(ref.target()))
      if (da->contentType() == DefinedAtom::typeResolver)
        return handleIFUNC(ref);
    return error_code::success();
  }

  ErrorOr<void> handlePC24(const Reference &ref) { return handleIFUNC(ref); }
};

#ifdef DYNLINKER
class DynamicGOTPLTPass LLVM_FINAL : public GOTPLTPass<DynamicGOTPLTPass> {
public:
  DynamicGOTPLTPass(const elf::Cpu0LinkingContext &ti, bool isExe)
      : GOTPLTPass(ti, isExe) { }

  const PLT0Atom *getPLT0() {
    if (_PLT0)
      return _PLT0;
    // Fill in the null entry.
    getNullGOT();
    _PLT0 = new (_file._alloc) Cpu0PLT0Atom(_file);
    _got0 = new (_file._alloc) Cpu0GOTAtom(_file, ".got.plt");
#if 0
    _PLT0->addReference(R_CPU0_GOT16, 0, _got0, -2); // debug
#endif
#ifndef NDEBUG
    _got0->_name = "__got0";
#endif
    return _PLT0;
  }

  const PLTAtom *getPLTEntry(const Atom *a) {
    auto plt = _pltMap.find(a);
    if (plt != _pltMap.end())
      return plt->second;
    auto ga = new (_file._alloc) Cpu0GOTAtom(_file, ".got.plt");
    ga->addReference(R_CPU0_JUMP_SLOT, 0, a, 0);
    auto pa = new (_file._alloc) Cpu0PLTAtom(_file, ".plt");
    getPLT0();  // add _PLT0 and _got0
    pa->addReference(LLD_R_CPU0_GOTRELINDEX, 0, ga, -2);
    // Set the starting address of the got entry to the second instruction in
    // the plt entry.
    ga->addReference(R_CPU0_32, 0, pa, 4);
#ifndef NDEBUG
    ga->_name = "__got_";
    ga->_name += a->name();
    pa->_name = "__plt_";
    pa->_name += a->name();
#endif
    _gotMap[a] = ga;
    _pltMap[a] = pa;
    _gotVector.push_back(ga);
    _pltVector.push_back(pa);
    return pa;
  }

  ErrorOr<void> handlePLT32(const Reference &ref) {
    // Turn this into a CALL24 to the PLT entry.
    // const_cast<Reference &>(ref).setKind(R_CPU0_CALL24);
    // Handle IFUNC.
    if (const DefinedAtom *da = 
            dyn_cast_or_null<const DefinedAtom>(ref.target()))
      if (da->contentType() == DefinedAtom::typeResolver)
        return handleIFUNC(ref);
    if (isa<const SharedLibraryAtom>(ref.target())) {
      const_cast<Reference &>(ref).setTarget(getPLTEntry(ref.target()));
      // Turn this into a PC24 to the PLT entry.
    #if 1
      const_cast<Reference &>(ref).setKind(R_CPU0_PC24);
    #endif
    }
    return error_code::success();
  }

  ErrorOr<void> handlePC24(const Reference &ref) {
    if (ref.target() && isa<SharedLibraryAtom>(ref.target()))
      return handlePLT32(ref);
    return handleIFUNC(ref);
  }

  const GOTAtom *getSharedGOT(const SharedLibraryAtom *sla) {
    auto got = _gotMap.find(sla);
    if (got == _gotMap.end()) {
      auto g = new (_file._alloc) Cpu0GOTAtom(_file, ".got.dyn");
      g->addReference(R_CPU0_GLOB_DAT, 0, sla, 0);
#ifndef NDEBUG
      g->_name = "__got_";
      g->_name += sla->name();
#endif
      _gotMap[sla] = g;
      _gotVector.push_back(g);
      return g;
    }
    return got->second;
  }

  void handleGOTPCREL(const Reference &ref) {
    const_cast<Reference &>(ref).setKind(R_CPU0_PC24);
    if (isa<UndefinedAtom>(ref.target()))
      const_cast<Reference &>(ref).setTarget(getNullGOT());
    else if (const DefinedAtom *da = dyn_cast<const DefinedAtom>(ref.target()))
      const_cast<Reference &>(ref).setTarget(getGOT(da));
    else if (const auto sla = dyn_cast<const SharedLibraryAtom>(ref.target()))
      const_cast<Reference &>(ref).setTarget(getSharedGOT(sla));
  }
};
#endif // DYNLINKER
} // end anon namespace

void elf::Cpu0LinkingContext::addPasses(PassManager &pm) const {
  switch (_outputFileType) {
  case llvm::ELF::ET_EXEC:
    if (_isStaticExecutable)
      pm.add(std::unique_ptr<Pass>(new StaticGOTPLTPass(*this, true)));
#ifdef DYNLINKER
    else
      pm.add(std::unique_ptr<Pass>(new DynamicGOTPLTPass(*this, true)));
    break;
  case llvm::ELF::ET_DYN:
    pm.add(std::unique_ptr<Pass>(new DynamicGOTPLTPass(*this, false)));
#endif // DYNLINKER
    break;
  case llvm::ELF::ET_REL:
    break;
  default:
    llvm_unreachable("Unhandled output file type");
  }
  ELFLinkingContext::addPasses(pm);
}

#define LLD_CASE(name) .Case(#name, llvm::ELF::name)

ErrorOr<Reference::Kind>
elf::Cpu0LinkingContext::relocKindFromString(StringRef str) const {
  int32_t ret = llvm::StringSwitch<int32_t>(str)
  LLD_CASE(R_CPU0_NONE)
  LLD_CASE(R_CPU0_24)
  LLD_CASE(R_CPU0_32)
  LLD_CASE(R_CPU0_HI16)
  LLD_CASE(R_CPU0_LO16)
  LLD_CASE(R_CPU0_GPREL16)
  LLD_CASE(R_CPU0_LITERAL)
  LLD_CASE(R_CPU0_GOT16)
  LLD_CASE(R_CPU0_PC24)
  LLD_CASE(R_CPU0_CALL16)
    .Case("LLD_R_CPU0_GOTRELINDEX", LLD_R_CPU0_GOTRELINDEX)
    .Default(-1);

  if (ret == -1)
    return make_error_code(yaml_reader_error::illegal_value);
  return ret;
}

#undef LLD_CASE

#define LLD_CASE(name) case llvm::ELF::name: return std::string(#name);

ErrorOr<std::string>
elf::Cpu0LinkingContext::stringFromRelocKind(Reference::Kind kind) const {
  switch (kind) {
  LLD_CASE(R_CPU0_NONE)
  LLD_CASE(R_CPU0_24)
  LLD_CASE(R_CPU0_32)
  LLD_CASE(R_CPU0_HI16)
  LLD_CASE(R_CPU0_LO16)
  LLD_CASE(R_CPU0_GPREL16)
  LLD_CASE(R_CPU0_LITERAL)
  LLD_CASE(R_CPU0_GOT16)
  LLD_CASE(R_CPU0_PC24)
  LLD_CASE(R_CPU0_CALL16)
  case LLD_R_CPU0_GOTRELINDEX:
    return std::string("LLD_R_CPU0_GOTRELINDEX");
  }

  return make_error_code(yaml_reader_error::illegal_value);
}
