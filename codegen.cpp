#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "include/KaleidoscopeJIT.h"
#include "include/AST.h"
#include "include/parser.h"
#include "include/lexer.h"
#include <cassert>
#include <cstdio>
#include <llvm/ADT/APFloat.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include <map>

using namespace llvm;
using namespace llvm::orc;

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value *> NamedValues;
static std::unique_ptr<FunctionPassManager> TheFPM;
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;
static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
std::unique_ptr<KaleidoscopeJIT> TheJIT;
ExitOnError ExitOnErr;


Value *LogErrorV(const char *Str) {
  // LogError<ExprAST>(Str);
  fprintf(stderr, "Error: %s\n", Str);
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
    break;
  }
  // if it was not a builtin operator then it was user defined
  // Emit a call to it
  Function *F = getFunction(std::string("binary")+m_Op);
  assert(F && "binary operator not found");

  Value *Ops[2] = { L, R };
  return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen() {
  // lookup name in global module table
  Function *CalleeF = getFunction(m_Callee);
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

  // if this is an operator then register it in
  // precedence table
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBianryPrecedence();

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

// generate code for conditional statements
Value *IfExprAST::codegen() {
  // since condition is just an expression
  Value *CondV = m_Cond->codegen();
  if (!CondV)
    return nullptr;

  // convert condition to bool
  CondV = Builder->CreateFCmpONE(CondV,
				 ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // create BasicBlock for then and else
  BasicBlock *ThenBB =
    BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // emit then value
  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = m_Then->codegen();
  if (!ThenV)
    return nullptr;
  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = Builder->GetInsertBlock();

  // emit else block
  TheFunction->insert(TheFunction->end(), ElseBB);
  Builder->SetInsertPoint(ElseBB);
  Value *ElseV = m_Else->codegen();
  if (!ElseV)
    return nullptr;
  Builder->CreateBr(ElseBB);
  ElseBB = Builder->GetInsertBlock();

  // emit merge block
  TheFunction->insert(TheFunction->end(), MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN =
    Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);

  return PN;
}

Value *ForExprAST::codegen() {
  // Emit start code before variable is in scope
  Value *StartVal = m_Start->codegen();
  if (!StartVal)
    return nullptr;

  // Make new basicblock for loop header, insertin after current
  // current block
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  BasicBlock *PreHeaderBB = Builder->GetInsertBlock();
  BasicBlock *LoopBB =
    BasicBlock::Create(*TheContext, "loop", TheFunction);
  // explicit fall to current block to loop block
  Builder->CreateBr(LoopBB);

  Builder->SetInsertPoint(LoopBB);
  // start PHInode with an entry for start
  PHINode *Variable = Builder->CreatePHI(Type::getDoubleTy(*TheContext),
					 2, m_VarName);
  Variable->addIncoming(StartVal, PreHeaderBB);

  // withing the loop variable is defined equal to phi node
  // if it shadows an existing variable then restore it
  Value *OldVal = NamedValues[m_VarName];
  NamedValues[m_VarName] = Variable;
  // emit body of the loop
  if (!m_Body->codegen())
    return nullptr;
  // emit step value
  Value *StepVal = nullptr;
  if (m_Step) {
    StepVal = m_Step->codegen();
    if (!StepVal)
      return nullptr;
  } else {
    // if no step specified then use 1
    StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
  }
  // add step value to looo variable
  Value *NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");
  // compute end condition
  Value *EndCond = m_End->codegen();
  if (!EndCond)
    return nullptr;

  // convert condition to bool by comparing it to 0
  EndCond = Builder->CreateFCmpONE(
				   EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  // after loop body
  BasicBlock *LoopEndBB = Builder->GetInsertBlock();
  BasicBlock *AfterBB =
    BasicBlock::Create(*TheContext, "afterloop", TheFunction);
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);
  // any new code will be inserted in AfterBB
  Builder->SetInsertPoint(AfterBB);

  // add a new entry to the PHInode for the backedge
  Variable->addIncoming(NextVar, LoopEndBB);

  // restore the shadowed variable
  if (OldVal)
    NamedValues[m_VarName] = OldVal;
  else
   NamedValues.erase(m_VarName);

  // `for loop` expr always return 0
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

// top level parsing and jit driver
void InitializeModuleAndManagers() {
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
void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()){
      fprintf(stderr, "Read a function definition\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      ExitOnErr(TheJIT->addModule(ThreadSafeModule(std::move(TheModule),
						   std::move(TheContext))));
      InitializeModuleAndManagers();
    }
  } else {
    getNextToken(); // for error recovery
  }
}

void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()){
      fprintf(stderr, "Read a function definition\n");
      FnIR->print(errs());
      fprintf(stderr, "\n");
      FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  } else {
    getNextToken(); // for error recovery
  }
}

void HandleTopLevelExpr() {
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

Value *UnaryExprAST::codegen() {
  Value *OperandV = m_Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("Unary") + m_Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");
  return Builder->CreateCall(F, OperandV, "unop");
}
