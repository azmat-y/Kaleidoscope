#include "include/AST.h"
#include "include/lexer.h"
#include <cstdio>
#include <memory>
#include <map>

// helper function for logging error messages
template <class T>
std::unique_ptr<T> LogError(const char *Str) {
  fprintf(stderr, "Error :%s\n", Str);
  return nullptr;
}

std::unique_ptr<ExprAST> ParseExpression();

// parenexpr := '(' expression ')'
std::unique_ptr<ExprAST> ParseParenExpr() {
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
std::unique_ptr<ExprAST> ParseIdentifierExpr() {
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
	return LogError<ExprAST>("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }
  getNextToken(); // eat )
  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

// numberexpr := number
std::unique_ptr<ExprAST> ParseNumberExpr() {
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
std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError<ExprAST>("Unknown token when expecting an expression.");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  }
}

static std::map<char, int> BinopPrecedence {
  {'<', 10},
  {'>', 10},
  {'-', 20},
  {'+', 20},
  {'*', 40},
  {'/', 40}
};

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
std::unique_ptr<ExprAST> ParseExpression() {
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
std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<PrototypeAST>("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken(); // eat funcion name
  if (CurTok != '(')
    return LogError<PrototypeAST>("Expected '(' in prototype");

  // read arguments
  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return LogError<PrototypeAST>("Expected '(' in prototype");

  // succesfull parsing
  getNextToken(); // eat )
  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

// definition := 'def' prototype expression
std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken();
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

  return nullptr;
}

// external := extern prototype
std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken();		// eat extern
  return ParsePrototype();
}

// toplevelexpr := expression
std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()){
    // make anonymous prototype
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}
