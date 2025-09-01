#pragma once
#include "location.h"
#include <istream>
#include <string>

enum TokenType {
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

struct Token {
  int Type = 0;
  std::string StrVal = "";
  double NumVal = 0.0;
  SourceLocation Loc;
};

class Lexer {
public:
  Lexer(std::istream &InputStream);
  Token getToken();
  // const std::string &getIdentifier() const { return IdentifierStr; }
  // double getNumVal() const { return NumVal; }
  SourceLocation getCurrentLocation() const { return CurrentLocation; }

private:
  std::istream &InputStream;
  // std::string IdentifierStr;
  // double NumVal;
  int LastChar = ' ';
  SourceLocation CurrentLocation = {1, 0};
  int advance();
};
