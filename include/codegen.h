#pragma once
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include <memory>

void InitializeModuleAndManagers();
void HandleDefinition();
void HandleExtern();
void HandleTopLevelExpr();

extern llvm::ExitOnError ExitOnErr;
/* extern std::unique_ptr<llvm::orc::KaleidoscopeJIT> TheJIT; */
extern std::unique_ptr<llvm::Module> TheModule;
extern std::unique_ptr<llvm::LLVMContext> TheContext;
extern std::vector<llvm::Function *> TopLevelFunctions;
extern std::unique_ptr<llvm::IRBuilder<>> Builder;
