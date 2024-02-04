#pragma once
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <cassert>
#include <memory>
#include <string>

using namespace llvm;
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
  bool m_IsOperator;
  unsigned m_Precedence;

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args,
	       bool IsOperator = false, unsigned Prec = 0)
  : m_Name(Name), m_Args(std::move(Args)), m_IsOperator(IsOperator),
  m_Precedence(Prec){}

  Function *codegen();
  const std::string &getName() const { return m_Name; }

  bool isUnaryOp() const { return m_IsOperator && m_Args.size() == 1;}
  bool isBinaryOp() const { return m_IsOperator && m_Args.size() == 2;}

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return m_Name[m_Name.size() - 1];
  }

  unsigned getBianryPrecedence() const { return m_Precedence;}
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

class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> m_Cond, m_Then, m_Else;
public:
  IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,
	    std::unique_ptr<ExprAST> Else)
    : m_Cond(std::move(Cond)), m_Then(std::move(Then)), m_Else(std::move(Else)) {}

  Value* codegen() override;
};

// for `for loops`
class ForExprAST : public ExprAST {
  std::string m_VarName;
  std::unique_ptr<ExprAST> m_Start, m_End, m_Step, m_Body;

public:
  ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
	     std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step,
	     std::unique_ptr<ExprAST> Body)
    : m_VarName(VarName), m_Start(std::move(Start)), m_End(std::move(End)),
      m_Step(std::move(Step)), m_Body(std::move(Body)) {}

  Value *codegen() override;
};
