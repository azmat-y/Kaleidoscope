#include "include/argparse.hpp"
#include "include/codegen.h"
#include "include/lexer.h"
#include "include/parser.h"
#include "llvm-c/Core.h"
#include "llvm/Support/TargetSelect.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <llvm-c/TargetMachine.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <memory>
#include <string>

using namespace llvm;
using namespace llvm::sys;

/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (true) {
    switch (CurTok.Type) {
    case tok_eof:
      return;
    case ';':
      getNextToken(); // ignore top level semicolon
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

int main(int argc, char **argv) {
  argparse::ArgumentParser program("kaleidoscope");
  program.add_description("AOT compiler for the Kaleidoscope language.");

  program.add_argument("--emit-ir")
      .help("Emit LLVM IR to stdout.")
      .default_value(false)
      .implicit_value(true);

  program.add_argument("input_file").help("The input source file to compile.");

  try {
    program.parse_args(argc, argv);
  } catch (const std::runtime_error &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << program;
    return 1;
  }

  std::string InputFile = program.get<std::string>("input_file");
  bool emitIR = program.get<bool>("--emit-ir");
  std::ifstream inf{InputFile};
  if (!inf) {
    std::cerr << "Error: Could not open file " << InputFile << std::endl;
    return 1;
  }
  TheLexer = std::make_unique<Lexer>(inf);
  // fprintf(stderr, "ready> ");
  getNextToken();
  InitializeModuleAndManagers();

  MainLoop();

  if (!TopLevelFunctions.empty()) {
    llvm::FunctionType *MainFT =
        llvm::FunctionType::get(Builder->getInt32Ty(), false);
    llvm::Function *MainF = llvm::Function::Create(
        MainFT, llvm::Function::ExternalLinkage, "main", TheModule.get());
    llvm::BasicBlock *BB =
        llvm::BasicBlock::Create(*TheContext, "entry", MainF);
    Builder->SetInsertPoint(BB);

    for (auto *Fn : TopLevelFunctions) {
      Builder->CreateCall(Fn, {});
    }
    Builder->CreateRet(llvm::ConstantInt::get(*TheContext, llvm::APInt(32, 0)));
  } else {
    fprintf(stderr, "Warning: No top-level expressions to execute, main "
                    "function will not be generated.\n");
  }
  if (emitIR)
    TheModule->print(llvm::errs(), nullptr);

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  auto TargetTriple = LLVMGetDefaultTargetTriple();
  TheModule->setTargetTriple(TargetTriple);
  std::string Error;
  auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);
  // print an error and exit if we could not find the requested
  // target triple
  if (!Target) {
    errs() << Error;
    return 1;
  }

  auto CPU = "generic";
  auto Features = "";

  TargetOptions opt;
  auto TargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features,
                                                   opt, Reloc::PIC_);

  TheModule->setDataLayout(TargetMachine->createDataLayout());

  // now write our output file
  auto Filename = "output.o";
  std::error_code EC;
  raw_fd_ostream dest(Filename, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Could not open file: " << EC.message();
    return 1;
  }

  legacy::PassManager pass;
  auto FileType = CodeGenFileType::ObjectFile;

  if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
    errs() << "TargetMachine can't emit a file of this type";
    return 1;
  }

  pass.run(*TheModule);
  dest.flush();

  // outs() << "Wrote " << Filename << "\n";

  LLVMDisposeMessage(TargetTriple);

  std::string LinkerCmd = "clang++ output.o runtime.o";
  int RetCode = system(LinkerCmd.c_str());
  if (RetCode != 0) {
    errs() << "Linking failed with exit code " << RetCode << '\n';
    return 1;
  }
  return 0;
}
