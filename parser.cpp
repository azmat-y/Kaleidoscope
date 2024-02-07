#include "include/AST.h"
#include "include/lexer.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <llvm/IR/Value.h>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>

// helper function for logging error messages
template <class T>
std::unique_ptr<T> LogError(const char *Str) {
  fprintf(stderr, "Error :%s\n", Str);
  return nullptr;
}

std::unique_ptr<ExprAST> ParseExpression();
std::unique_ptr<ExprAST> ParseUnary();

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

std::unique_ptr<ExprAST> ParseIfExpr();

// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> ParseForExpr() {
  getNextToken(); // eat for
  if (CurTok != tok_identifier)
    return LogError<ExprAST>("Expected identifier after for");

  std::string IdName = IdentifierStr;
  getNextToken(); // eat identifier

  if (CurTok != '=')
    return LogError<ExprAST>("Expected `=` after identifer");
  getNextToken(); // eat `=`

  auto Start = ParseExpression();
  if (!Start)
    return nullptr;
  if (CurTok != ',')
    return LogError<ExprAST>("Expected `,` after start");
  getNextToken();

  auto End = ParseExpression();
  if (!End)
    return nullptr;

  // step value is optional
  std::unique_ptr<ExprAST> Step;
  if (CurTok == ',') {
    getNextToken();
    Step = ParseExpression();
    if (!Step)
      return nullptr;
  }

  if (CurTok != tok_in)
    return LogError<ExprAST>("Expected `in` after for");
  getNextToken(); // eat `in`

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<ForExprAST>(IdName, std::move(Start),
				      std::move(End), std::move(Step),
				      std::move(Body));
}

std::unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat the var keyword
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

  // Check if there is atleast one variable is there
  if (CurTok != tok_identifier)
    return LogError<ExprAST>("expected identifier after var");

  while (true) {
    std::string Name = IdentifierStr;
    getNextToken(); // eat identifer

    // read the optional initializer
    std::unique_ptr<ExprAST> Init;
    if (CurTok == '=') {
      getNextToken(); // eat assignment operator
      Init = ParseExpression();
      if (!Init) return nullptr;
    }
    VarNames.push_back(std::make_pair(Name, std::move(Init)));

    // when reach end of var list exit the loop
    if (CurTok != ',') break;
    getNextToken(); // eat ','

    if (CurTok != tok_identifier)
    return LogError<ExprAST>("expected identifier list after var");
  }
  // now we use an 'in'
  if (CurTok != tok_in)
    return LogError<ExprAST>("expected 'in' keyword after 'var'");
  getNextToken(); // eat 'in'

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;
  return std::make_unique<VarExprAST>(std::move(VarNames),
                                      std::move(Body));
}

/*
  primary
  := identifierexpr
  := numberexpr
  := parenexpr
  := ifexpr
  := forexpr
  := varexpr
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
  case tok_if:
    return ParseIfExpr();
  case tok_for:
    return ParseForExpr();
  case tok_var:
    return ParseVarExpr();
  }
}

std::map<char, int> BinopPrecedence {
  {'=', 2},
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
  auto LHS = ParseUnary();
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

    // Parse the Unary expression after binar operator
    auto RHS =  ParseUnary();
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
  std::string FnName;

  unsigned Kind = 0; // 0 = identifer, 1 = unary, 2 = binary
  unsigned BinaryPrecedence = 30;
  switch (CurTok) {
  default:
    return LogError<PrototypeAST>("Expected function name in prototyp");
  case tok_identifier:
    FnName = IdentifierStr;
    Kind = 0;
    getNextToken(); // consume identifer
    break;

  case tok_unary:
    getNextToken(); // consume keyword
    if (!isascii(CurTok))
      return LogError<PrototypeAST>("Expected unary operator");
    FnName = "unary";
    FnName += (char)CurTok;
    Kind = 1;
    getNextToken(); // consume Op
    break;

  case tok_binary:
    getNextToken(); // consume keyword
    if (!isascii(CurTok))
      return LogError<PrototypeAST>("Expected binary operator");
    FnName = "binary";
    FnName += (char)CurTok;
    Kind = 2;
    getNextToken(); // consume operator

    // read precedence if present
    if (CurTok == tok_number) {
      if (NumVal < 1 || NumVal > 100)
	return LogError<PrototypeAST>("Invalid precedence: must be betweeen 1..100");
      BinaryPrecedence = (unsigned)NumVal;
      getNextToken(); // consume the precedence
    }
    break;
  }

  if (CurTok != '(')
    return LogError<PrototypeAST>("Expected '(' in prototype");

  // read arguments
  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return LogError<PrototypeAST>("Expected ')' in prototype");

  // succesfull parsing
  getNextToken(); // eat )

  // verify right number of names for operator
  if (Kind && ArgNames.size() != Kind)
    return LogError<PrototypeAST>("Invalid number of operands for operator");

  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames), Kind != 0,
					BinaryPrecedence);
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

// ifexpr ::= 'if' expression 'then' expression 'else' expression
std::unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat 'if'

  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != tok_then)
    return LogError<ExprAST>("expected `then`");
  getNextToken(); // eat 'then'

  auto Then = ParseExpression();
  if (!Then)
    return nullptr;

  if (CurTok != tok_else)
    return LogError<ExprAST>("expected `else`");
  getNextToken(); // eat 'else'

  auto Else = ParseExpression();
  if (!Else)
    return nullptr;

  return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));
}

// unary
// ::= primary
// ::= '!' unary
std::unique_ptr<ExprAST> ParseUnary() {
  // if CurTok is not an operator then it must be an operator
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // if this is a unary operator then parse it
  int Opc = CurTok;
  getNextToken();
  if (auto Operand = ParsePrimary())
    return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}
