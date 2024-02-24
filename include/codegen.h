#pragma once
#include "llvm/Support/Error.h"
#include "KaleidoscopeJIT.h"
#include <memory>

void InitializeModuleAndManagers();
void HandleDefinition();
void HandleExtern();
void HandleTopLevelExpr();

extern llvm::ExitOnError ExitOnErr;
extern std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
