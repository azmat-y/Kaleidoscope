#include "include/lexer.h"
#include <istream>
#include <string>

int Lexer::advance() {
  int LastChar = InputStream.get();
  if (LastChar == '\n' || LastChar == '\r') {
    CurrentLocation.Line++;
    CurrentLocation.Col = 0;
  } else
    CurrentLocation.Col++;
  return LastChar;
}

Lexer::Lexer(std::istream &in) : InputStream(in) { LastChar = ' '; }

Token Lexer::getToken() {
  // skip whitespace
  while (isspace(LastChar))
    LastChar = advance();

  Token T;
  T.Loc = CurrentLocation;

  // recognise keywords like "def", "extern" and Identifier [a-zA/0-Z0]
  if (isalpha(LastChar)) {
    T.StrVal = LastChar;
    while (isalnum(LastChar = advance()))
      T.StrVal += LastChar;

    if (T.StrVal == "def")
      T.Type = tok_def;
    else if (T.StrVal == "extern")
      T.Type = tok_extern;
    else if (T.StrVal == "if")
      T.Type = tok_if;
    else if (T.StrVal == "then")
      T.Type = tok_then;
    else if (T.StrVal == "else")
      T.Type = tok_else;
    else if (T.StrVal == "for")
      T.Type = tok_for;
    else if (T.StrVal == "in")
      T.Type = tok_in;
    else if (T.StrVal == "binary")
      T.Type = tok_binary;
    else if (T.StrVal == "unary")
      T.Type = tok_unary;
    else if (T.StrVal == "var")
      T.Type = tok_var;
    else
      T.Type = tok_identifier;
    return T;
  }

  // recognise numeric literals
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = advance();
    } while (isdigit(LastChar) || LastChar == '.');

    T.NumVal = strtod(NumStr.c_str(), 0);
    T.Type = tok_number;
    return T;
  }

  // recognise commments
  if (LastChar == '#') {
    // comment lasts until end of line
    do
      LastChar = advance();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return getToken();
  }

  // Incase input doesn't match any of the above cases

  // check for EOF
  if (LastChar == EOF) {
    T.Type = tok_eof;
    return T;
  }
  // handle for operator character like '+', '-' etc, just return ascii_value

  T.Type = LastChar;
  LastChar = advance(); // move the input seed
  return T;
}
