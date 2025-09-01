#pragma once
#include "location.h"
#include <cassert>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
// when we have a parser we will define & build an AST
inline raw_ostream &Indent(raw_ostream &O, int size) {
  return O << std::string(size, ' ');
}
// Blueprint for AST
class ExprAST {
  SourceLocation Loc;

public:
  ExprAST(SourceLocation Loc) : Loc(Loc) {}
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
  int getLine() const { return Loc.Line; }
  int getCol() const { return Loc.Col; }
  SourceLocation getLocation() const { return Loc; }
  virtual raw_ostream &dump(raw_ostream &out, int ind) {
    return out << ':' << getLine() << ':' << getCol() << '\n';
  }
};

// for numeric literals
class NumberExprAST : public ExprAST {
  double m_Val;

public:
  NumberExprAST(SourceLocation Loc, double Val) : ExprAST(Loc), m_Val(Val) {}
  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    return ExprAST::dump(out << m_Val, ind);
  }
};

// for refrencing a variable like "x"
class VariableExprAST : public ExprAST {
  std::string m_Name;

public:
  VariableExprAST(SourceLocation Loc, const std::string &Name)
      : ExprAST(Loc), m_Name(Name) {}
  Value *codegen() override;
  const std::string &getName() const { return m_Name; }
  raw_ostream &dump(raw_ostream &out, int ind) override {
    return ExprAST::dump(out << m_Name, ind);
  }
};

class UnaryExprAST : public ExprAST {
  char m_Opcode;
  std::unique_ptr<ExprAST> m_Operand;

public:
  UnaryExprAST(SourceLocation Loc, char Opcode,
               std::unique_ptr<ExprAST> Operand)
      : ExprAST(Loc), m_Opcode(Opcode), m_Operand(std::move(Operand)) {}

  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "unary" << m_Opcode, ind);
    m_Operand->dump(out, ind + 1);
    return out;
  }
};

// for Binary Expressions like x+y
class BinaryExprAST : public ExprAST {
  char m_Op;
  std::unique_ptr<ExprAST> m_LHS, m_RHS;

public:
  BinaryExprAST(SourceLocation Loc, char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : ExprAST(Loc), m_Op(Op), m_LHS(std::move(LHS)), m_RHS(std::move(RHS)) {}
  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "binary" << m_Op, ind);
    m_LHS->dump(Indent(out, ind) << "LHS:", ind + 1);
    m_RHS->dump(Indent(out, ind) << "RHS:", ind + 1);
    return out;
  }
};

// for Call Expressions like functions calls say, factorial(5)
class CallExprAST : public ExprAST {
  std::string m_Callee;
  std::vector<std::unique_ptr<ExprAST>> m_Args;

public:
  CallExprAST(SourceLocation Loc, const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : ExprAST(Loc), m_Callee(Callee), m_Args(std::move(Args)) {}
  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "call " << m_Callee, ind);
    for (const auto &Arg : m_Args)
      Arg->dump(Indent(out, ind + 1), ind + 1);
    return out;
  }
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
        m_Precedence(Prec) {}

  Function *codegen();
  const std::string &getName() const { return m_Name; }

  bool isUnaryOp() const { return m_IsOperator && m_Args.size() == 1; }
  bool isBinaryOp() const { return m_IsOperator && m_Args.size() == 2; }

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    return m_Name[m_Name.size() - 1];
  }

  unsigned getBianryPrecedence() const { return m_Precedence; }
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
  IfExprAST(SourceLocation Loc, std::unique_ptr<ExprAST> Cond,
            std::unique_ptr<ExprAST> Then, std::unique_ptr<ExprAST> Else)
      : ExprAST(Loc), m_Cond(std::move(Cond)), m_Then(std::move(Then)),
        m_Else(std::move(Else)) {}

  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "if", ind);
    m_Cond->dump(Indent(out, ind) << "Cond:", ind + 1);
    m_Then->dump(Indent(out, ind) << "Then:", ind + 1);
    m_Else->dump(Indent(out, ind) << "Else:", ind + 1);
    return out;
  }
};

// for `for loops`
class ForExprAST : public ExprAST {
  std::string m_VarName;
  std::unique_ptr<ExprAST> m_Start, m_End, m_Step, m_Body;

public:
  ForExprAST(SourceLocation Loc, const std::string &VarName,
             std::unique_ptr<ExprAST> Start, std::unique_ptr<ExprAST> End,
             std::unique_ptr<ExprAST> Step, std::unique_ptr<ExprAST> Body)
      : ExprAST(Loc), m_VarName(VarName), m_Start(std::move(Start)),
        m_End(std::move(End)), m_Step(std::move(Step)),
        m_Body(std::move(Body)) {}

  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "for", ind);
    m_Start->dump(Indent(out, ind) << "Cond:", ind + 1);
    m_End->dump(Indent(out, ind) << "End:", ind + 1);
    m_Step->dump(Indent(out, ind) << "Step:", ind + 1);
    m_Body->dump(Indent(out, ind) << "Body:", ind + 1);
    return out;
  }
};

class VarExprAST : public ExprAST {
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> m_VarNames;
  std::unique_ptr<ExprAST> m_Body;

public:
  VarExprAST(
      SourceLocation Loc,
      std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
      std::unique_ptr<ExprAST> Body)
      : ExprAST(Loc), m_VarNames(std::move(VarNames)), m_Body(std::move(Body)) {
  }

  Value *codegen() override;
  raw_ostream &dump(raw_ostream &out, int ind) override {
    ExprAST::dump(out << "var", ind);
    for (const auto &NamedVar : m_VarNames)
      NamedVar.second->dump(Indent(out, ind) << NamedVar.first << ':', ind + 1);
    m_Body->dump(Indent(out, ind) << "Body:", ind + 1);
    return out;
  }
};
