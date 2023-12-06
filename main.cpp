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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <map>

    // The Lexer return [0-255] for unknown tokens but the following for the
    // knowns

using namespace llvm;

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
};

// for Binary Expressions like x+y
class BinaryExprAST : public ExprAST {
  char m_Op;
  std::unique_ptr<ExprAST> m_LHS, m_RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
    : m_Op(Op), m_LHS(std::move(LHS)), m_RHS(std::move(RHS)) {}
};

// for Call Expressions like functions calls say, factorial(5)
class CallExprAST : public ExprAST {
  std::string m_Callee;
  std::vector<std::unique_ptr<ExprAST>> m_Args;

public:
  CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args)
    : m_Callee(Callee), m_Args(std::move(Args)) {}
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

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<IRBuilder<>> Builder(TheContext);
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

// numberexpr := number
static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(Result);
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
    auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

// for top level parsing
static void HandleDefinition() {
  if (ParseDefinition()) {
    fprintf(stderr, "Parsed a function definition\n");
  } else {
    getNextToken(); // for error recovery
  }
}

static void HandleExtern() {
  if (ParseExtern())
    fprintf(stderr, "Parsed an extern\n");
  else
    getNextToken();
}

static void HandleTopLevelExpr() {
  if (ParseTopLevelExpr())
    fprintf(stderr, "Parsed a top level expression\n");
  else
    getNextToken();
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
  BinopPrecedence['<'] = 10;
  BinopPrecedence['>'] = 10;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['*'] = 40;
  BinopPrecedence['/'] = 40;

  fprintf(stderr, "ready> ");
  getNextToken();

  MainLoop();

  return 0;
}
