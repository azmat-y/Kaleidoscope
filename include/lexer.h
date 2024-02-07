#pragma once
#include <string>


enum Token {
  tok_eof = -1,

  // command
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,

  // conditionals
  tok_if = -6,
  tok_then = -7,
  tok_else = -8,

  // `for` loops
  tok_for = -9,
  tok_in = -10,

  // for  operators
  tok_binary = -11,
  tok_unary = -12,

  // for local variables
  tok_var = -13
};

extern std::string IdentifierStr;
extern double NumVal;

int gettok();

extern int CurTok;
int getNextToken();
