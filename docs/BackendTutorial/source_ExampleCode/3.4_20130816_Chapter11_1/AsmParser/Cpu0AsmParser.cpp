//===-- Cpu0AsmParser.cpp - Parse Cpu0 assembly to MCInst instructions ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Cpu0MCTargetDesc.h"
#include "Cpu0RegisterInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCTargetAsmParser.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

namespace {
class Cpu0AssemblerOptions {
public:
  Cpu0AssemblerOptions():
    aTReg(1), reorder(true), macro(true) {
  }

  bool isReorder() {return reorder;}
  void setReorder() {reorder = true;}
  void setNoreorder() {reorder = false;}

  bool isMacro() {return macro;}
  void setMacro() {macro = true;}
  void setNomacro() {macro = false;}

private:
  unsigned aTReg;
  bool reorder;
  bool macro;
};
}

namespace {
class Cpu0AsmParser : public MCTargetAsmParser {
  MCSubtargetInfo &STI;
  MCAsmParser &Parser;
  Cpu0AssemblerOptions Options;


#define GET_ASSEMBLER_HEADER
#include "Cpu0GenAsmMatcher.inc"

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               SmallVectorImpl<MCParsedAsmOperand*> &Operands,
                               MCStreamer &Out, unsigned &ErrorInfo,
                               bool MatchingInlineAsm);

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc);

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc,
                        SmallVectorImpl<MCParsedAsmOperand*> &Operands);

  bool parseMathOperation(StringRef Name, SMLoc NameLoc,
                        SmallVectorImpl<MCParsedAsmOperand*> &Operands);

  bool ParseDirective(AsmToken DirectiveID);

  Cpu0AsmParser::OperandMatchResultTy
  parseMemOperand(SmallVectorImpl<MCParsedAsmOperand*>&);

  bool ParseOperand(SmallVectorImpl<MCParsedAsmOperand*> &,
                    StringRef Mnemonic);

  int tryParseRegister(StringRef Mnemonic);

  bool tryParseRegisterOperand(SmallVectorImpl<MCParsedAsmOperand*> &Operands,
                               StringRef Mnemonic);

  bool needsExpansion(MCInst &Inst);

  void expandInstruction(MCInst &Inst, SMLoc IDLoc,
                         SmallVectorImpl<MCInst> &Instructions);
  void expandLoadImm(MCInst &Inst, SMLoc IDLoc,
                     SmallVectorImpl<MCInst> &Instructions);
  void expandLoadAddressImm(MCInst &Inst, SMLoc IDLoc,
                            SmallVectorImpl<MCInst> &Instructions);
  void expandLoadAddressReg(MCInst &Inst, SMLoc IDLoc,
                            SmallVectorImpl<MCInst> &Instructions);
  bool reportParseError(StringRef ErrorMsg);

  bool parseMemOffset(const MCExpr *&Res);
  bool parseRelocOperand(const MCExpr *&Res);

  bool parseDirectiveSet();

  bool parseSetAtDirective();
  bool parseSetNoAtDirective();
  bool parseSetMacroDirective();
  bool parseSetNoMacroDirective();
  bool parseSetReorderDirective();
  bool parseSetNoReorderDirective();

  MCSymbolRefExpr::VariantKind getVariantKind(StringRef Symbol);

  int matchRegisterName(StringRef Symbol);

  int matchRegisterByNumber(unsigned RegNum, StringRef Mnemonic);

  unsigned getReg(int RC,int RegNo);

public:
  Cpu0AsmParser(MCSubtargetInfo &sti, MCAsmParser &parser)
    : MCTargetAsmParser(), STI(sti), Parser(parser) {
    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

  MCAsmParser &getParser() const { return Parser; }
  MCAsmLexer &getLexer() const { return Parser.getLexer(); }

};
}

namespace {

/// Cpu0Operand - Instances of this class represent a parsed Cpu0 machine
/// instruction.
class Cpu0Operand : public MCParsedAsmOperand {

  enum KindTy {
    k_CondCode,
    k_CoprocNum,
    k_Immediate,
    k_Memory,
    k_PostIndexRegister,
    k_Register,
    k_Token
  } Kind;

  Cpu0Operand(KindTy K) : MCParsedAsmOperand(), Kind(K) {}

  union {
    struct {
      const char *Data;
      unsigned Length;
    } Tok;

    struct {
      unsigned RegNum;
    } Reg;

    struct {
      const MCExpr *Val;
    } Imm;

    struct {
      unsigned Base;
      const MCExpr *Off;
    } Mem;
  };

  SMLoc StartLoc, EndLoc;

public:
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::CreateReg(getReg()));
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const{
    // Add as immediate when possible.  Null MCExpr = 0.
    if (Expr == 0)
      Inst.addOperand(MCOperand::CreateImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::CreateImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::CreateExpr(Expr));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    addExpr(Inst,Expr);
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::CreateReg(getMemBase()));

    const MCExpr *Expr = getMemOff();
    addExpr(Inst,Expr);
  }

  bool isReg() const { return Kind == k_Register; }
  bool isImm() const { return Kind == k_Immediate; }
  bool isToken() const { return Kind == k_Token; }
  bool isMem() const { return Kind == k_Memory; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  unsigned getReg() const {
    assert((Kind == k_Register) && "Invalid access!");
    return Reg.RegNum;
  }

  const MCExpr *getImm() const {
    assert((Kind == k_Immediate) && "Invalid access!");
    return Imm.Val;
  }

  unsigned getMemBase() const {
    assert((Kind == k_Memory) && "Invalid access!");
    return Mem.Base;
  }

  const MCExpr *getMemOff() const {
    assert((Kind == k_Memory) && "Invalid access!");
    return Mem.Off;
  }

  static Cpu0Operand *CreateToken(StringRef Str, SMLoc S) {
    Cpu0Operand *Op = new Cpu0Operand(k_Token);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static Cpu0Operand *CreateReg(unsigned RegNum, SMLoc S, SMLoc E) {
    Cpu0Operand *Op = new Cpu0Operand(k_Register);
    Op->Reg.RegNum = RegNum;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static Cpu0Operand *CreateImm(const MCExpr *Val, SMLoc S, SMLoc E) {
    Cpu0Operand *Op = new Cpu0Operand(k_Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static Cpu0Operand *CreateMem(unsigned Base, const MCExpr *Off,
                                 SMLoc S, SMLoc E) {
    Cpu0Operand *Op = new Cpu0Operand(k_Memory);
    Op->Mem.Base = Base;
    Op->Mem.Off = Off;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const { return StartLoc; }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const { return EndLoc; }

  virtual void print(raw_ostream &OS) const {
    llvm_unreachable("unimplemented!");
  }
};
}

bool Cpu0AsmParser::needsExpansion(MCInst &Inst) {

  switch(Inst.getOpcode()) {
    case Cpu0::LoadImm32Reg:
    case Cpu0::LoadAddr32Imm:
    case Cpu0::LoadAddr32Reg:
      return true;
    default:
      return false;
  }
}

void Cpu0AsmParser::expandInstruction(MCInst &Inst, SMLoc IDLoc,
                        SmallVectorImpl<MCInst> &Instructions){
  switch(Inst.getOpcode()) {
    case Cpu0::LoadImm32Reg:
      return expandLoadImm(Inst, IDLoc, Instructions);
    case Cpu0::LoadAddr32Imm:
      return expandLoadAddressImm(Inst,IDLoc,Instructions);
    case Cpu0::LoadAddr32Reg:
      return expandLoadAddressReg(Inst,IDLoc,Instructions);
    }
}

void Cpu0AsmParser::expandLoadImm(MCInst &Inst, SMLoc IDLoc,
                                  SmallVectorImpl<MCInst> &Instructions){
  MCInst tmpInst;
  const MCOperand &ImmOp = Inst.getOperand(1);
  assert(ImmOp.isImm() && "expected immediate operand kind");
  const MCOperand &RegOp = Inst.getOperand(0);
  assert(RegOp.isReg() && "expected register operand kind");

  int ImmValue = ImmOp.getImm();
  tmpInst.setLoc(IDLoc);
  if ( 0 <= ImmValue && ImmValue <= 65535) {
    // for 0 <= j <= 65535.
    // li d,j => ori d,$zero,j
    tmpInst.setOpcode(Cpu0::ORi);
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(
              MCOperand::CreateReg(Cpu0::ZERO));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue));
    Instructions.push_back(tmpInst);
  } else if ( ImmValue < 0 && ImmValue >= -32768) {
    // for -32768 <= j < 0.
    // li d,j => addiu d,$zero,j
    tmpInst.setOpcode(Cpu0::ADDiu); //TODO:no ADDiu64 in td files?
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(
              MCOperand::CreateReg(Cpu0::ZERO));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue));
    Instructions.push_back(tmpInst);
  } else {
    // for any other value of j that is representable as a 32-bit integer.
    // li d,j => lui d,hi16(j)
    //           ori d,d,lo16(j)
    tmpInst.setOpcode(Cpu0::LUi);
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm((ImmValue & 0xffff0000) >> 16));
    Instructions.push_back(tmpInst);
    tmpInst.clear();
    tmpInst.setOpcode(Cpu0::ORi);
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue & 0xffff));
    tmpInst.setLoc(IDLoc);
    Instructions.push_back(tmpInst);
  }
}

void Cpu0AsmParser::expandLoadAddressReg(MCInst &Inst, SMLoc IDLoc,
                                         SmallVectorImpl<MCInst> &Instructions){
  MCInst tmpInst;
  const MCOperand &ImmOp = Inst.getOperand(2);
  assert(ImmOp.isImm() && "expected immediate operand kind");
  const MCOperand &SrcRegOp = Inst.getOperand(1);
  assert(SrcRegOp.isReg() && "expected register operand kind");
  const MCOperand &DstRegOp = Inst.getOperand(0);
  assert(DstRegOp.isReg() && "expected register operand kind");
  int ImmValue = ImmOp.getImm();
  if ( -32768 <= ImmValue && ImmValue <= 32767) {
    // for -32768 <= j < 32767.
    //la d,j(s) => addiu d,s,j
    tmpInst.setOpcode(Cpu0::ADDiu); //TODO:no ADDiu64 in td files?
    tmpInst.addOperand(MCOperand::CreateReg(DstRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateReg(SrcRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue));
    Instructions.push_back(tmpInst);
  } else {
    // for any other value of j that is representable as a 32-bit integer.
    // la d,j(s) => lui d,hi16(j)
    //              ori d,d,lo16(j)
    //              add d,d,s
    tmpInst.setOpcode(Cpu0::LUi);
    tmpInst.addOperand(MCOperand::CreateReg(DstRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm((ImmValue & 0xffff0000) >> 16));
    Instructions.push_back(tmpInst);
    tmpInst.clear();
    tmpInst.setOpcode(Cpu0::ORi);
    tmpInst.addOperand(MCOperand::CreateReg(DstRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateReg(DstRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue & 0xffff));
    Instructions.push_back(tmpInst);
    tmpInst.clear();
    tmpInst.setOpcode(Cpu0::ADD);
    tmpInst.addOperand(MCOperand::CreateReg(DstRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateReg(DstRegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateReg(SrcRegOp.getReg()));
    Instructions.push_back(tmpInst);
  }
}

void Cpu0AsmParser::expandLoadAddressImm(MCInst &Inst, SMLoc IDLoc,
                                         SmallVectorImpl<MCInst> &Instructions){
  MCInst tmpInst;
  const MCOperand &ImmOp = Inst.getOperand(1);
  assert(ImmOp.isImm() && "expected immediate operand kind");
  const MCOperand &RegOp = Inst.getOperand(0);
  assert(RegOp.isReg() && "expected register operand kind");
  int ImmValue = ImmOp.getImm();
  if ( -32768 <= ImmValue && ImmValue <= 32767) {
    // for -32768 <= j < 32767.
    //la d,j => addiu d,$zero,j
    tmpInst.setOpcode(Cpu0::ADDiu);
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(
              MCOperand::CreateReg(Cpu0::ZERO));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue));
    Instructions.push_back(tmpInst);
  } else {
    // for any other value of j that is representable as a 32-bit integer.
    // la d,j => lui d,hi16(j)
    //           ori d,d,lo16(j)
    tmpInst.setOpcode(Cpu0::LUi);
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm((ImmValue & 0xffff0000) >> 16));
    Instructions.push_back(tmpInst);
    tmpInst.clear();
    tmpInst.setOpcode(Cpu0::ORi);
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateReg(RegOp.getReg()));
    tmpInst.addOperand(MCOperand::CreateImm(ImmValue & 0xffff));
    Instructions.push_back(tmpInst);
  }
}

bool Cpu0AsmParser::
MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                        SmallVectorImpl<MCParsedAsmOperand*> &Operands,
                        MCStreamer &Out, unsigned &ErrorInfo,
                        bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult = MatchInstructionImpl(Operands, Inst, ErrorInfo,
                                              MatchingInlineAsm);

  switch (MatchResult) {
  default: break;
  case Match_Success: {
    if (needsExpansion(Inst)) {
      SmallVector<MCInst, 4> Instructions;
      expandInstruction(Inst, IDLoc, Instructions);
      for(unsigned i =0; i < Instructions.size(); i++){
        Out.EmitInstruction(Instructions[i]);
      }
    } else {
        Inst.setLoc(IDLoc);
        Out.EmitInstruction(Inst);
      }
    return false;
  }
  case Match_MissingFeature:
    Error(IDLoc, "instruction requires a CPU feature not currently enabled");
    return true;
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0U) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");

      ErrorLoc = ((Cpu0Operand*)Operands[ErrorInfo])->getStartLoc();
      if (ErrorLoc == SMLoc()) ErrorLoc = IDLoc;
    }

    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction");
  }
  return true;
}

int Cpu0AsmParser::matchRegisterName(StringRef Name) {

   int CC;
    CC = StringSwitch<unsigned>(Name)
      .Case("zero",  Cpu0::ZERO)
      .Case("at",  Cpu0::AT)
      .Case("v0",  Cpu0::V0)
      .Case("v1",  Cpu0::V1)
      .Case("a0",  Cpu0::A0)
      .Case("a1",  Cpu0::A1)
      .Case("t9",  Cpu0::T9)
      .Case("s0",  Cpu0::S0)
      .Case("s1",  Cpu0::S1)
      .Case("s2",  Cpu0::S2)
      .Case("gp",  Cpu0::GP)
      .Case("fp",  Cpu0::FP)
      .Case("sw",  Cpu0::SW)
      .Case("sp",  Cpu0::SP)
      .Case("lr",  Cpu0::LR)
      .Case("pc",  Cpu0::PC)
      .Default(-1);

  if (CC != -1)
    return CC;

  return -1;
}

unsigned Cpu0AsmParser::getReg(int RC,int RegNo) {
  return *(getContext().getRegisterInfo()->getRegClass(RC).begin() + RegNo);
}

int Cpu0AsmParser::matchRegisterByNumber(unsigned RegNum, StringRef Mnemonic) {
  if (RegNum > 15)
    return -1;

  return getReg(Cpu0::CPURegsRegClassID, RegNum);
}

int Cpu0AsmParser::tryParseRegister(StringRef Mnemonic) {
  const AsmToken &Tok = Parser.getTok();
  int RegNum = -1;

  if (Tok.is(AsmToken::Identifier)) {
    std::string lowerCase = Tok.getString().lower();
    RegNum = matchRegisterName(lowerCase);
  } else if (Tok.is(AsmToken::Integer))
    RegNum = matchRegisterByNumber(static_cast<unsigned>(Tok.getIntVal()),
                                   Mnemonic.lower());
    else
      return RegNum;  //error
  return RegNum;
}

bool Cpu0AsmParser::
  tryParseRegisterOperand(SmallVectorImpl<MCParsedAsmOperand*> &Operands,
                          StringRef Mnemonic){

  SMLoc S = Parser.getTok().getLoc();
  int RegNo = -1;

    RegNo = tryParseRegister(Mnemonic);
  if (RegNo == -1)
    return true;

  Operands.push_back(Cpu0Operand::CreateReg(RegNo, S,
      Parser.getTok().getLoc()));
  Parser.Lex(); // Eat register token.
  return false;
}

bool Cpu0AsmParser::ParseOperand(SmallVectorImpl<MCParsedAsmOperand*>&Operands,
                                 StringRef Mnemonic) {
  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  OperandMatchResultTy ResTy = MatchOperandParserImpl(Operands, Mnemonic);
  if (ResTy == MatchOperand_Success)
    return false;
  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (ResTy == MatchOperand_ParseFail)
    return true;

  switch (getLexer().getKind()) {
  default:
    Error(Parser.getTok().getLoc(), "unexpected token in operand");
    return true;
  case AsmToken::Dollar: {
    // parse register
    SMLoc S = Parser.getTok().getLoc();
    Parser.Lex(); // Eat dollar token.
    // parse register operand
    if (!tryParseRegisterOperand(Operands, Mnemonic)) {
      if (getLexer().is(AsmToken::LParen)) {
        // check if it is indexed addressing operand
        Operands.push_back(Cpu0Operand::CreateToken("(", S));
        Parser.Lex(); // eat parenthesis
        if (getLexer().isNot(AsmToken::Dollar))
          return true;

        Parser.Lex(); // eat dollar
        if (tryParseRegisterOperand(Operands, Mnemonic))
          return true;

        if (!getLexer().is(AsmToken::RParen))
          return true;

        S = Parser.getTok().getLoc();
        Operands.push_back(Cpu0Operand::CreateToken(")", S));
        Parser.Lex();
      }
      return false;
    }
    // maybe it is a symbol reference
    StringRef Identifier;
    if (Parser.parseIdentifier(Identifier))
      return true;

    SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

    MCSymbol *Sym = getContext().GetOrCreateSymbol("$" + Identifier);

    // Otherwise create a symbol ref.
    const MCExpr *Res = MCSymbolRefExpr::Create(Sym, MCSymbolRefExpr::VK_None,
                                                getContext());

    Operands.push_back(Cpu0Operand::CreateImm(Res, S, E));
    return false;
  }
  case AsmToken::Identifier:
  case AsmToken::LParen:
  case AsmToken::Minus:
  case AsmToken::Plus:
  case AsmToken::Integer:
  case AsmToken::String: {
     // quoted label names
    const MCExpr *IdVal;
    SMLoc S = Parser.getTok().getLoc();
    if (getParser().parseExpression(IdVal))
      return true;
    SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
    Operands.push_back(Cpu0Operand::CreateImm(IdVal, S, E));
    return false;
  }
  case AsmToken::Percent: {
    // it is a symbol reference or constant expression
    const MCExpr *IdVal;
    SMLoc S = Parser.getTok().getLoc(); // start location of the operand
    if (parseRelocOperand(IdVal))
      return true;

    SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

    Operands.push_back(Cpu0Operand::CreateImm(IdVal, S, E));
    return false;
  } // case AsmToken::Percent
  } // switch(getLexer().getKind())
  return true;
}

bool Cpu0AsmParser::parseRelocOperand(const MCExpr *&Res) {

  Parser.Lex(); // eat % token
  const AsmToken &Tok = Parser.getTok(); // get next token, operation
  if (Tok.isNot(AsmToken::Identifier))
    return true;

  std::string Str = Tok.getIdentifier().str();

  Parser.Lex(); // eat identifier
  // now make expression from the rest of the operand
  const MCExpr *IdVal;
  SMLoc EndLoc;

  if (getLexer().getKind() == AsmToken::LParen) {
    while (1) {
      Parser.Lex(); // eat '(' token
      if (getLexer().getKind() == AsmToken::Percent) {
        Parser.Lex(); // eat % token
        const AsmToken &nextTok = Parser.getTok();
        if (nextTok.isNot(AsmToken::Identifier))
          return true;
        Str += "(%";
        Str += nextTok.getIdentifier();
        Parser.Lex(); // eat identifier
        if (getLexer().getKind() != AsmToken::LParen)
          return true;
      } else
        break;
    }
    if (getParser().parseParenExpression(IdVal,EndLoc))
      return true;

    while (getLexer().getKind() == AsmToken::RParen)
      Parser.Lex(); // eat ')' token

  } else
    return true; // parenthesis must follow reloc operand

  // Check the type of the expression
  if (const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(IdVal)) {
    // it's a constant, evaluate lo or hi value
    int Val = MCE->getValue();
    if (Str == "lo") {
      Val = Val & 0xffff;
    } else if (Str == "hi") {
      Val = (Val & 0xffff0000) >> 16;
    }
    Res = MCConstantExpr::Create(Val, getContext());
    return false;
  }

  if (const MCSymbolRefExpr *MSRE = dyn_cast<MCSymbolRefExpr>(IdVal)) {
    // it's a symbol, create symbolic expression from symbol
    StringRef Symbol = MSRE->getSymbol().getName();
    MCSymbolRefExpr::VariantKind VK = getVariantKind(Str);
    Res = MCSymbolRefExpr::Create(Symbol,VK,getContext());
    return false;
  }
  return true;
}

bool Cpu0AsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                  SMLoc &EndLoc) {

  StartLoc = Parser.getTok().getLoc();
  RegNo = tryParseRegister("");
  EndLoc = Parser.getTok().getLoc();
  return (RegNo == (unsigned)-1);
}

bool Cpu0AsmParser::parseMemOffset(const MCExpr *&Res) {

  SMLoc S;

  switch(getLexer().getKind()) {
  default:
    return true;
  case AsmToken::Integer:
  case AsmToken::Minus:
  case AsmToken::Plus:
    return (getParser().parseExpression(Res));
  case AsmToken::Percent:
    return parseRelocOperand(Res);
  case AsmToken::LParen:
    return false;  // it's probably assuming 0
  }
  return true;
}

// eg, 12($sp) or 12(la)
Cpu0AsmParser::OperandMatchResultTy Cpu0AsmParser::parseMemOperand(
               SmallVectorImpl<MCParsedAsmOperand*>&Operands) {

  const MCExpr *IdVal = 0;
  SMLoc S;
  // first operand is the offset
  S = Parser.getTok().getLoc();

  if (parseMemOffset(IdVal))
    return MatchOperand_ParseFail;

  const AsmToken &Tok = Parser.getTok(); // get next token
  if (Tok.isNot(AsmToken::LParen)) {
    Cpu0Operand *Mnemonic = static_cast<Cpu0Operand*>(Operands[0]);
    if (Mnemonic->getToken() == "la") {
      SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer()-1);
      Operands.push_back(Cpu0Operand::CreateImm(IdVal, S, E));
      return MatchOperand_Success;
    }
    Error(Parser.getTok().getLoc(), "'(' expected");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat '(' token.

  const AsmToken &Tok1 = Parser.getTok(); // get next token
  if (Tok1.is(AsmToken::Dollar)) {
    Parser.Lex(); // Eat '$' token.
    if (tryParseRegisterOperand(Operands,"")) {
      Error(Parser.getTok().getLoc(), "unexpected token in operand");
      return MatchOperand_ParseFail;
    }

  } else {
    Error(Parser.getTok().getLoc(), "unexpected token in operand");
    return MatchOperand_ParseFail;
  }

  const AsmToken &Tok2 = Parser.getTok(); // get next token
  if (Tok2.isNot(AsmToken::RParen)) {
    Error(Parser.getTok().getLoc(), "')' expected");
    return MatchOperand_ParseFail;
  }

  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);

  Parser.Lex(); // Eat ')' token.

  if (IdVal == 0)
    IdVal = MCConstantExpr::Create(0, getContext());

  // now replace register operand with the mem operand
  Cpu0Operand* op = static_cast<Cpu0Operand*>(Operands.back());
  int RegNo = op->getReg();
  // remove register from operands
  Operands.pop_back();
  // and add memory operand
  Operands.push_back(Cpu0Operand::CreateMem(RegNo, IdVal, S, E));
  delete op;
  return MatchOperand_Success;
}

MCSymbolRefExpr::VariantKind Cpu0AsmParser::getVariantKind(StringRef Symbol) {

  MCSymbolRefExpr::VariantKind VK
                   = StringSwitch<MCSymbolRefExpr::VariantKind>(Symbol)
    .Case("hi",          MCSymbolRefExpr::VK_Cpu0_ABS_HI)
    .Case("lo",          MCSymbolRefExpr::VK_Cpu0_ABS_LO)
    .Case("gp_rel",      MCSymbolRefExpr::VK_Cpu0_GPREL)
    .Case("call24",      MCSymbolRefExpr::VK_Cpu0_GOT_CALL)
    .Case("got",         MCSymbolRefExpr::VK_Cpu0_GOT)
    .Case("tlsgd",       MCSymbolRefExpr::VK_Cpu0_TLSGD)
    .Case("tlsldm",      MCSymbolRefExpr::VK_Cpu0_TLSLDM)
    .Case("dtprel_hi",   MCSymbolRefExpr::VK_Cpu0_DTPREL_HI)
    .Case("dtprel_lo",   MCSymbolRefExpr::VK_Cpu0_DTPREL_LO)
    .Case("gottprel",    MCSymbolRefExpr::VK_Cpu0_GOTTPREL)
    .Case("tprel_hi",    MCSymbolRefExpr::VK_Cpu0_TPREL_HI)
    .Case("tprel_lo",    MCSymbolRefExpr::VK_Cpu0_TPREL_LO)
    .Case("got_disp",    MCSymbolRefExpr::VK_Cpu0_GOT_DISP)
    .Case("got_page",    MCSymbolRefExpr::VK_Cpu0_GOT_PAGE)
    .Case("got_ofst",    MCSymbolRefExpr::VK_Cpu0_GOT_OFST)
    .Case("hi(%neg(%gp_rel",    MCSymbolRefExpr::VK_Cpu0_GPOFF_HI)
    .Case("lo(%neg(%gp_rel",    MCSymbolRefExpr::VK_Cpu0_GPOFF_LO)
    .Default(MCSymbolRefExpr::VK_None);

  return VK;
}

bool Cpu0AsmParser::
parseMathOperation(StringRef Name, SMLoc NameLoc,
                   SmallVectorImpl<MCParsedAsmOperand*> &Operands) {
  // split the format
  size_t Start = Name.find('.'), Next = Name.rfind('.');
  StringRef Format1 = Name.slice(Start, Next);
  // and add the first format to the operands
  Operands.push_back(Cpu0Operand::CreateToken(Format1, NameLoc));
  // now for the second format
  StringRef Format2 = Name.slice(Next, StringRef::npos);
  Operands.push_back(Cpu0Operand::CreateToken(Format2, NameLoc));

  // set the format for the first register
//  setFpFormat(Format1);

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (ParseOperand(Operands, Name)) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();
      return Error(Loc, "unexpected token in argument list");
    }

    if (getLexer().isNot(AsmToken::Comma)) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();
      return Error(Loc, "unexpected token in argument list");

    }
    Parser.Lex();  // Eat the comma.

    // Parse and remember the operand.
    if (ParseOperand(Operands, Name)) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();
      return Error(Loc, "unexpected token in argument list");
    }
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    Parser.eatToEndOfStatement();
    return Error(Loc, "unexpected token in argument list");
  }

  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

bool Cpu0AsmParser::
ParseInstruction(ParseInstructionInfo &Info, StringRef Name, SMLoc NameLoc,
                 SmallVectorImpl<MCParsedAsmOperand*> &Operands) {

  // Create the leading tokens for the mnemonic, split by '.' characters.
  size_t Start = 0, Next = Name.find('.');
  StringRef Mnemonic = Name.slice(Start, Next);

  Operands.push_back(Cpu0Operand::CreateToken(Mnemonic, NameLoc));

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (ParseOperand(Operands, Name)) {
      SMLoc Loc = getLexer().getLoc();
      Parser.eatToEndOfStatement();
      return Error(Loc, "unexpected token in argument list");
    }

    while (getLexer().is(AsmToken::Comma) ) {
      Parser.Lex();  // Eat the comma.

      // Parse and remember the operand.
      if (ParseOperand(Operands, Name)) {
        SMLoc Loc = getLexer().getLoc();
        Parser.eatToEndOfStatement();
        return Error(Loc, "unexpected token in argument list");
      }
    }
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    Parser.eatToEndOfStatement();
    return Error(Loc, "unexpected token in argument list");
  }

  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

bool Cpu0AsmParser::reportParseError(StringRef ErrorMsg) {
   SMLoc Loc = getLexer().getLoc();
   Parser.eatToEndOfStatement();
   return Error(Loc, ErrorMsg);
}

bool Cpu0AsmParser::parseSetReorderDirective() {
  Parser.Lex();
  // if this is not the end of the statement, report error
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    reportParseError("unexpected token in statement");
    return false;
  }
  Options.setReorder();
  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

bool Cpu0AsmParser::parseSetNoReorderDirective() {
    Parser.Lex();
    // if this is not the end of the statement, report error
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      reportParseError("unexpected token in statement");
      return false;
    }
    Options.setNoreorder();
    Parser.Lex(); // Consume the EndOfStatement
    return false;
}

bool Cpu0AsmParser::parseSetMacroDirective() {
  Parser.Lex();
  // if this is not the end of the statement, report error
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    reportParseError("unexpected token in statement");
    return false;
  }
  Options.setMacro();
  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

bool Cpu0AsmParser::parseSetNoMacroDirective() {
  Parser.Lex();
  // if this is not the end of the statement, report error
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    reportParseError("`noreorder' must be set before `nomacro'");
    return false;
  }
  if (Options.isReorder()) {
    reportParseError("`noreorder' must be set before `nomacro'");
    return false;
  }
  Options.setNomacro();
  Parser.Lex(); // Consume the EndOfStatement
  return false;
}
bool Cpu0AsmParser::parseDirectiveSet() {

  // get next token
  const AsmToken &Tok = Parser.getTok();

  if (Tok.getString() == "reorder") {
    return parseSetReorderDirective();
  } else if (Tok.getString() == "noreorder") {
    return parseSetNoReorderDirective();
  } else if (Tok.getString() == "macro") {
    return parseSetMacroDirective();
  } else if (Tok.getString() == "nomacro") {
    return parseSetNoMacroDirective();
  }
  return true;
}

bool Cpu0AsmParser::ParseDirective(AsmToken DirectiveID) {

  if (DirectiveID.getString() == ".ent") {
    // ignore this directive for now
    Parser.Lex();
    return false;
  }

  if (DirectiveID.getString() == ".end") {
    // ignore this directive for now
    Parser.Lex();
    return false;
  }

  if (DirectiveID.getString() == ".frame") {
    // ignore this directive for now
    Parser.eatToEndOfStatement();
    return false;
  }

  if (DirectiveID.getString() == ".set") {
    return parseDirectiveSet();
  }

  if (DirectiveID.getString() == ".fmask") {
    // ignore this directive for now
    Parser.eatToEndOfStatement();
    return false;
  }

  if (DirectiveID.getString() == ".mask") {
    // ignore this directive for now
    Parser.eatToEndOfStatement();
    return false;
  }

  if (DirectiveID.getString() == ".gpword") {
    // ignore this directive for now
    Parser.eatToEndOfStatement();
    return false;
  }

  return true;
}

extern "C" void LLVMInitializeCpu0AsmParser() {
  RegisterMCAsmParser<Cpu0AsmParser> X(TheCpu0Target);
  RegisterMCAsmParser<Cpu0AsmParser> Y(TheCpu0elTarget);
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "Cpu0GenAsmMatcher.inc"

