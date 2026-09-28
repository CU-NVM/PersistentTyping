#ifndef CAST_FRONTEND_ACTION_HPP
#define CAST_FRONTEND_ACTION_HPP

#include <llvm/ADT/StringRef.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/Support/raw_ostream.h>
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "../consumer/consumer.h"
#include "../inclusiondirective/inclusiondirective.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "llvm/IR/Module.h"
// #include "clang/lib/CodeGen/CodeGenModule.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include <memory>
#include <vector>
#include <string>

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"


#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace llvm;
namespace clang
{
    class CompilerInstance;
}

class CastNumaFrontendAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) override;
  //void EndSourceFileAction() override;
private:
  clang::Rewriter TheRewriter;
  IncludeTracker* TheIncludeTracker;
};



#endif