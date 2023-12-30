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
};

extern std::string IdentifierStr;
extern double NumVal;

int gettok();

extern int CurTok;
int getNextToken();
