#include "include/lexer.h"
#include <string>


std::string IdentifierStr; // filed if tok_identifier
double NumVal;		  // filed if tok_number

// return the next token from std input
int gettok() {
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
    if (IdentifierStr == "if")
      return tok_if;
    if (IdentifierStr == "then")
      return tok_then;
    if (IdentifierStr == "else")
      return tok_else;
    if (IdentifierStr == "for")
      return tok_for;
    if (IdentifierStr == "in")
      return tok_in;
    if (IdentifierStr == "binary")
      return tok_binary;
    if (IdentifierStr == "unary")
      return tok_unary;
    if (IdentifierStr == "var")
      return tok_var;
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

int CurTok;
int getNextToken() {
  return CurTok = gettok();
}
