#pragma once
#include "llvm/Support/Error.h"
#include "llvm/IR/Module.h"
#include "KaleidoscopeJIT.h"
#include <memory>

void InitializeModuleAndManagers();
void HandleDefinition();
void HandleExtern();
void HandleTopLevelExpr();

extern llvm::ExitOnError ExitOnErr;
extern std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT;
extern std::unique_ptr<llvm::Module> TheModule;
