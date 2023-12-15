#include "KaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/Support/Error.h>
#include <memory>
#include <string>
#include <vector>
#include <map>

    // The Lexer return [0-255] for unknown tokens but the following for the
    // knowns

using namespace llvm;
using namespace llvm::orc;

enum Token {
  tok_eof = -1,

  // command
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

static std::string IdentifierStr; // filed if tok_identifier
static double NumVal;		  // filed if tok_number

// return the next token from std input
static int gettok() {
  static int LastChar = ' ';

  // skip whitespace
  while (isspace(LastChar))
    LastChar = getchar();

  // recognise keywords like "def", "extern" and Identifier [a-zA/0-Z0]
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum(LastChar = getchar()))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;
    if (IdentifierStr == "extern")
      return tok_extern;
    return tok_identifier;
  }

  // recognise numeric literals
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  // recognise commments
  if (LastChar == '#') {
    // comment lasts until end of line
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  // Incase input doesn't match any of the above cases

  // check for EOF
  if (LastChar == EOF)
    return tok_eof;

  // handle for operator character like '+', '-' etc, just return ascii_value
  int ThisChar = LastChar;
  LastChar = getchar(); 	// move the input seed
  return ThisChar;

}

// when we have a parser we will define & build an AST

// Blueprint for AST
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

// for numeric literals
class NumberExprAST : public ExprAST {
  double m_Val;
public:
  NumberExprAST(double Val) : m_Val(Val) {}
  Value *codegen() override;
};

// for refrencing a variable like "x"
class VariableExprAST : public ExprAST {
  std::string m_Name;
public:
  VariableExprAST(const std::string &Name) : m_Name(Name) {}
  Value *codegen() override;
};

// for Binary Expressions like x+y
class BinaryExprAST : public ExprAST {
  char m_Op;
  std::unique_ptr<ExprAST> m_LHS, m_RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
    : m_Op(Op), m_LHS(std::move(LHS)), m_RHS(std::move(RHS)) {}
  Value *codegen() override;
};

// for Call Expressions like functions calls say, factorial(5)
class CallExprAST : public ExprAST {
  std::string m_Callee;
  std::vector<std::unique_ptr<ExprAST>> m_Args;

public:
  CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
    : m_Callee(Callee), m_Args(std::move(Args)) {}
  Value *codegen() override;
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class PrototypeAST {
  std::string m_Name;
  std::vector<std::string> m_Args;

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args)
    : m_Name(Name), m_Args(std::move(Args)) {}

  Function *codegen();
  const std::string &getName() const { return m_Name; }
};

// for *Function Definition*
class FunctionAST {
  std::unique_ptr<PrototypeAST> m_Proto;
  std::unique_ptr<ExprAST> m_Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
	      std::unique_ptr<ExprAST> Body)
    : m_Proto(std::move(Proto)), m_Body(std::move(Body)) {}
  Function *codegen();
};

// now we will build the parser to build our AST

// Helper functions to look one token ahead
static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}

// Helper functions for error handling
std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

// parenexpr := '(' expression ')'
static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat (
  auto V = ParseExpression();
  if (!V)
    return nullptr;
  getNextToken(); // eat )
  return  V;
}

// for parsing identifiers, funcitons calls
// identifier := identifier
//            := identifier '(' expression ')'
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  getNextToken(); // eat Identifier
  if (CurTok != '(')		// this implies it is a variable
    return std::make_unique<VariableExprAST>(IdName);

  // when it is a function call
  getNextToken(); // eat (
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
	Args.push_back(std::move(Arg));
      else
	return nullptr;
      if (CurTok == ')')
	break;
      if (CurTok != ',')
	return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }
  getNextToken(); // eat )
  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

// numberexpr := number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(Result);
}
/*
  primary
  := identifierexpr
  := numberexpr
  := parenexpr
 */
static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("Unknown token when expecting an expression.");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  }
}

static std::map<char, int> BinopPrecedence;

// get token precedence
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // make sure it is a declared Binop
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
					      std::unique_ptr<ExprAST> LHS);

//   expression := primary [binoprhs]
static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}

// binoprhs := ( op primary)
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
					      std::unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    // when RHS is empty
    if (TokPrec < ExprPrec)
      return LHS;

    // now we know it is a binop
    int Binop = CurTok;
    getNextToken(); // eat binop

    auto RHS =  ParsePrimary();
    if (!RHS)
      return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec+1, std::move(RHS));
      if (!RHS)
	return nullptr;
    }
    LHS = std::make_unique<BinaryExprAST>(Binop, std::move(LHS), std::move(RHS));
  }
}

/*
  prototype
  := id '(' [id] ')'
 */
static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken(); // eat funcion name
  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  // read arguments
  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return LogErrorP("Expected '(' in prototype");

  // succesfull parsing
  getNextToken(); // eat )
  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

// definition := 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken();
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

  return nullptr;
}

// external := extern prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken();		// eat extern
  return ParsePrototype();
}

// toplevelexpr := expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()){
    // make anonymous prototype
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

// for codegen
static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static std::unique_ptr<KaleidoscopeJIT> TheJIT;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static ExitOnError ExitOnErr;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

Function *getFunction(std::string Name) {
  // has the function already added to current module
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // can existing prototypes codgen this function
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  return nullptr;
}

Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(m_Val));
}

Value *VariableExprAST::codegen() {
  Value *V = NamedValues[m_Name];
  if (!V)
    return LogErrorV("Unknown Variable Name");
 return V;
}

Value *BinaryExprAST::codegen() {
  Value *L = m_LHS->codegen();
  Value *R = m_RHS->codegen();

  if (!L || !R)
    return nullptr;

  switch (m_Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '/':
    return Builder->CreateFDiv(L, R, "divtmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext),
				  "booltmp");
  default:
    return LogErrorV("invalid binary operator");
  }
}

Value *CallExprAST::codegen() {
  // lookup name in global module table
  Function *CalleeF = TheModule->getFunction(m_Callee);
  if (!CalleeF)
    return LogErrorV("Unknown Function refrenced");

  // if arguments do not match
  if (CalleeF->arg_size() != m_Args.size())
    return LogErrorV("Incorrect # of arguments");

  std::vector<Value*> ArgsV;
  for (unsigned i=0, e = m_Args.size(); i!=e; i++) {
    ArgsV.push_back(m_Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen() {
  // our language only supports doubles so functions will be of form
  // double(double, double ...)
  std::vector<Type*> Doubles(m_Args.size(), Type::getDoubleTy(*TheContext));

  FunctionType *FT =
    FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
    Function::Create(FT, Function::ExternalLinkage, m_Name, TheModule.get());

  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(m_Args[Idx++]);

  return F;
}

Function *FunctionAST::codegen() {

  auto &P = *m_Proto;
  FunctionProtos[m_Proto->getName()] = std::move(m_Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // now that we've checked that funnction body is empty
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // record fun arguments in Namedvalues
  NamedValues.clear();
  for (auto &Arg : TheFunction->args())
    NamedValues[std::string(Arg.getName())] = &Arg;

  if (Value *RetVal = m_Body->codegen()) {
    // finish the function
    Builder->CreateRet(RetVal);

    // validate the generated code for consistency
    verifyFunction(*TheFunction);

    // run the optimizer on the function
    TheFPM->run(*TheFunction, *TheFAM);
    return TheFunction;
  }

  /// reading erorr remove the function
  TheFunction->eraseFromParent();
  return nullptr;
}

// top level parsing and jit driver
static void InitializeModuleAndManagers() {
  // open new context and module
  TheContext = std::make_unique<LLVMContext>();
  TheModule = std::make_unique<Module>("Kaliedoscope JIT", *TheContext);
  TheModule->setDataLayout(TheJIT->getDataLayout());

  // create a new builder for module
  Builder = std::make_unique<IRBuilder<>>(*TheContext);

  // create a new pass and analysis managers
  TheFPM = std::make_unique<FunctionPassManager>();
  TheLAM = std::make_unique<LoopAnalysisManager>();
  TheFAM = std::make_unique<FunctionAnalysisManager>();
  TheCGAM = std::make_unique<CGSCCAnalysisManager>();
  TheMAM = std::make_unique<ModuleAnalysisManager>();
  ThePIC = std::make_unique<PassInstrumentationCallbacks>();
  TheSI = std::make_unique<StandardInstrumentations>(*TheContext, true); // debugging logging

  TheSI->registerCallbacks(*ThePIC, TheMAM.get());
  // add transform passes and do simple optimizations

  TheFPM->addPass(InstCombinePass());
  TheFPM->addPass(ReassociatePass());

  // eliminate subexpression
  TheFPM->addPass(GVNPass());

  // simplify control flow graph
  TheFPM->addPass(SimplifyCFGPass());

  PassBuilder PB;
  PB.registerModuleAnalyses(*TheMAM);
  PB.registerFunctionAnalyses(*TheFAM);
  PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}


// for top level parsing
static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()){
      fprintf(stderr, "Read a function definition\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    getNextToken(); // for error recovery
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()){
      fprintf(stderr, "Read a function definition\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    getNextToken(); // for error recovery
  }
}

static void HandleTopLevelExpr() {
  if (auto FnAST = ParseTopLevelExpr()) {
    if (FnAST->codegen()) {
      // create a Resouce Tracker to track JIT'd memory allocated to our anonymous
      // expression so we free it after executing
      auto RT = TheJIT->getMainJITDylib().createResourceTracker();
      auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
      ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
      InitializeModuleAndManagers();

      // search JIT for __anon_expr symbol
      auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

      // get symbol address and cast it to right address i.e. double
      double (*FP)() = ExprSymbol.getAddress().toPtr<double (*)()>();
      fprintf(stderr, "Evaluated to %f\n", FP());

      // delete resources
      ExitOnErr(RT->remove());
    }
  } else {
    getNextToken();
  }
}


/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:
      return;
    case ';':
      getNextToken();		// ignore top level semicolon
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpr();
    }
  }
}

int main() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();

  BinopPrecedence['<'] = 10;
  BinopPrecedence['>'] = 10;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['*'] = 40;
  BinopPrecedence['/'] = 40;

  fprintf(stderr, "ready> ");
  getNextToken();

  // InitializeModuleAndManagers();

  MainLoop();

  TheModule->print(errs(), nullptr);

  return 0;
}
