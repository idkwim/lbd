//====------ MachineBlockFrequencyInfo.cpp - MBB Frequency Analysis ------====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/Analysis/BlockFrequencyImpl.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

#ifndef NDEBUG
enum GVDAGType {
  GVDT_None,
  GVDT_Fraction,
  GVDT_Integer
};

static cl::opt<GVDAGType>
ViewMachineBlockFreqPropagationDAG("view-machine-block-freq-propagation-dags",
                                   cl::Hidden,
          cl::desc("Pop up a window to show a dag displaying how machine block "
                   "frequencies propagate through the CFG."),
          cl::values(
            clEnumValN(GVDT_None, "none",
                       "do not display graphs."),
            clEnumValN(GVDT_Fraction, "fraction", "display a graph using the "
                       "fractional block frequency representation."),
            clEnumValN(GVDT_Integer, "integer", "display a graph using the raw "
                       "integer fractional block frequency representation."),
            clEnumValEnd));

namespace llvm {

template <>
struct GraphTraits<MachineBlockFrequencyInfo *> {
  typedef const MachineBasicBlock NodeType;
  typedef MachineBasicBlock::const_succ_iterator ChildIteratorType;
  typedef MachineFunction::const_iterator nodes_iterator;

  static inline
  const NodeType *getEntryNode(const MachineBlockFrequencyInfo *G) {
    return G->getFunction()->begin();
  }

  static ChildIteratorType child_begin(const NodeType *N) {
    return N->succ_begin();
  }

  static ChildIteratorType child_end(const NodeType *N) {
    return N->succ_end();
  }

  static nodes_iterator nodes_begin(const MachineBlockFrequencyInfo *G) {
    return G->getFunction()->begin();
  }

  static nodes_iterator nodes_end(const MachineBlockFrequencyInfo *G) {
    return G->getFunction()->end();
  }
};

template<>
struct DOTGraphTraits<MachineBlockFrequencyInfo*> :
    public DefaultDOTGraphTraits {
  explicit DOTGraphTraits(bool isSimple=false) :
    DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const MachineBlockFrequencyInfo *G) {
    return G->getFunction()->getName();
  }

  std::string getNodeLabel(const MachineBasicBlock *Node,
                           const MachineBlockFrequencyInfo *Graph) {
    std::string Result;
    raw_string_ostream OS(Result);

    OS << Node->getName().str() << ":";
    switch (ViewMachineBlockFreqPropagationDAG) {
    case GVDT_Fraction:
      Graph->getBlockFreq(Node).print(OS);
      break;
    case GVDT_Integer:
      OS << Graph->getBlockFreq(Node).getFrequency();
      break;
    case GVDT_None:
      llvm_unreachable("If we are not supposed to render a graph we should "
                       "never reach this point.");
    }

    return Result;
  }
};


} // end namespace llvm
#endif

INITIALIZE_PASS_BEGIN(MachineBlockFrequencyInfo, "machine-block-freq",
                      "Machine Block Frequency Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfo)
INITIALIZE_PASS_END(MachineBlockFrequencyInfo, "machine-block-freq",
                    "Machine Block Frequency Analysis", true, true)

char MachineBlockFrequencyInfo::ID = 0;


MachineBlockFrequencyInfo::
MachineBlockFrequencyInfo() :MachineFunctionPass(ID) {
  initializeMachineBlockFrequencyInfoPass(*PassRegistry::getPassRegistry());
  MBFI = new BlockFrequencyImpl<MachineBasicBlock, MachineFunction,
                                MachineBranchProbabilityInfo>();
}

MachineBlockFrequencyInfo::~MachineBlockFrequencyInfo() {
  delete MBFI;
}

void MachineBlockFrequencyInfo::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineBranchProbabilityInfo>();
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool MachineBlockFrequencyInfo::runOnMachineFunction(MachineFunction &F) {
  MachineBranchProbabilityInfo &MBPI =
    getAnalysis<MachineBranchProbabilityInfo>();
  MBFI->doFunction(&F, &MBPI);
#ifndef NDEBUG
  if (ViewMachineBlockFreqPropagationDAG != GVDT_None) {
    view();
  }
#endif
  return false;
}

/// Pop up a ghostview window with the current block frequency propagation
/// rendered using dot.
void MachineBlockFrequencyInfo::view() const {
// This code is only for debugging.
#ifndef NDEBUG
  ViewGraph(const_cast<MachineBlockFrequencyInfo *>(this),
            "MachineBlockFrequencyDAGs");
#else
  errs() << "MachineBlockFrequencyInfo::view is only available in debug builds "
    "on systems with Graphviz or gv!\n";
#endif // NDEBUG
}

BlockFrequency MachineBlockFrequencyInfo::
getBlockFreq(const MachineBasicBlock *MBB) const {
  return MBFI->getBlockFreq(MBB);
}

MachineFunction *MachineBlockFrequencyInfo::getFunction() const {
  return MBFI->Fn;
}
