#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "include/AST.h"
#include "include/parser.h"
#include "include/lexer.h"
#include "include/codegen.h"
#include <cstdio>
#include <llvm/ADT/APFloat.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassInstrumentation.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include <map>
#include <memory>

using namespace llvm;
using namespace llvm::orc;

std::unique_ptr<Module> TheModule;
std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, AllocaInst*> NamedValues;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
// std::unique_ptr<KaleidoscopeJIT> TheJIT;
ExitOnError ExitOnErr;

AllocaInst *CreateEntryBlockAlloca(Function *TheFucntion,
				    StringRef VarName) {
  IRBuilder<> TmpB(&TheFucntion->getEntryBlock(),
                   TheFucntion->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr,
			   VarName);
}

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
  // return Builder->CreateLoad(V->getAllocatedType(), V, m_Name.c_str());
  return Builder->CreateLoad(Type::getDoubleTy(*TheContext), V, m_Name.c_str());
}

Value *BinaryExprAST::codegen() {

  // Special edge case because we don't want LHS as an expression
  if (m_Op == '=') {
    // This assume we're building without RTTI because LLVM builds that way by
    // default. If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    VariableExprAST *LHSE = static_cast<VariableExprAST*>(m_LHS.get());
    if (!LHSE)
      return LogErrorV("Unknown variable name");

    // codegen the RHS
    Value *Val = m_RHS->codegen();
    if (!Val)
      return nullptr;

    // look up the name
    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");
    Builder->CreateStore(Val, Variable);
    return Val;
  }
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

  Value *Ops[] = { L, R };
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
  for (auto &Arg : TheFunction->args()){
    // create an Alloca for this variable
    // AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());
   // Store the initial value into the alloca.
    Builder->CreateStore(&Arg, Alloca);

    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  if (Value *RetVal = m_Body->codegen()) {
    // finish the function
    Builder->CreateRet(RetVal);

    // validate the generated code for consistency
    verifyFunction(*TheFunction, &llvm::errs());

    // run the optimizer on the function
    // TheFPM->run(*TheFunction, *TheFAM);
    return TheFunction;
  }

  /// reading erorr remove the function
  TheFunction->eraseFromParent();

  if (P.isBinaryOp())
    BinopPrecedence.erase(P.getOperatorName());
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
  Builder->CreateBr(MergeBB);
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

  // Make new basicblock for loop header, insertin after current
  // current block
  Function *TheFunction = Builder->GetInsertBlock()->getParent();
  // create an alloca for the variable in entry block
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, m_VarName);
  // Emit start code before variable is in scope
  Value *StartVal = m_Start->codegen();
  if (!StartVal)
    return nullptr;
  // Store the value into alloca
  Builder->CreateStore(StartVal, Alloca);

  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);
  // explicit fall to current block to loop block
  Builder->CreateBr(LoopBB);

  Builder->SetInsertPoint(LoopBB);

  // withing the loop variable is defined equal to phi node
  // if it shadows an existing variable then restore it
  AllocaInst *OldVal = NamedValues[m_VarName];
  NamedValues[m_VarName] = Alloca;
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
  // compute end condition
  Value *EndCond = m_End->codegen();
  if (!EndCond)
    return nullptr;

  // add step value to looo variable
  // reload, increament and restore the alloca
  Value *CurVar = Builder->CreateLoad(Type::getDoubleTy(*TheContext), Alloca,
				      m_VarName.c_str());
  Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);
  // convert condition to bool by comparing it to 0
  EndCond = Builder->CreateFCmpONE(
				   EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
  // after loop body
  BasicBlock *AfterBB =
    BasicBlock::Create(*TheContext, "afterloop", TheFunction);
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);
  // any new code will be inserted in AfterBB
  Builder->SetInsertPoint(AfterBB);

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
  TheModule = std::make_unique<Module>("my cool jit", *TheContext);
  Builder = std::make_unique<IRBuilder<>>(*TheContext);
}


// for top level parsing
void HandleDefinition() {
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
    FnAST->codegen();
  } else {
    getNextToken();
  }
}

Value *UnaryExprAST::codegen() {
  Value *OperandV = m_Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + m_Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");
  return Builder->CreateCall(F, OperandV, "unop");
}

Value *VarExprAST::codegen() {
  std::vector<AllocaInst*> OldBindings;
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // register all variables and emit their initializer
  for (unsigned i = 0, e = m_VarNames.size(); i != e; ++i) {
    const std::string &VarName = m_VarNames[i].first;
    ExprAST *Init = m_VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.

    Value *InitVal;
    if (Init) {
      InitVal = Init->codegen();
      if (!InitVal)
	return nullptr;
    } else {
      InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
    }
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    // remember the old bindings so that we can restore them
    OldBindings.push_back(NamedValues[VarName]);

    // remember the bindings
    NamedValues[VarName] = Alloca;
  }

   // Codegen the body, now that all vars are in scope.
  Value *BodyVal = m_Body->codegen();
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  for (unsigned i = 0, e = m_VarNames.size(); i != e; ++i)
    NamedValues[m_VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  return BodyVal;

}
