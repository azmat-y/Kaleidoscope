#include "include/lexer.h"
#include "include/codegen.h"
#include "llvm/Support/TargetSelect.h"
#include "include/KaleidoscopeJIT.h"
#include <cstdio>
#include <llvm/MC/TargetRegistry.h>


using namespace llvm;
using namespace llvm::sys;

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

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/*
call to c functions from our language through forward declaration
 */

/// putchard - putchar that takes a double and returns 0.
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}

int main() {

  fprintf(stderr, "ready> ");
  getNextToken();

  InitializeModuleAndManagers();

  MainLoop();

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  auto  TargetTriple = LLVMGetDefaultTargetTriple();

  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);
  // print an error and exit if we could not find the requested
  // target triple
  if (!Target) {
    errs() << Error;
    return 1;
  }

  return 0;
}
