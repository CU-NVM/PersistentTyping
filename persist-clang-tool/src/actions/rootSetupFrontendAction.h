#ifndef ROOTSETUPFRONTENDACTION_H
#define ROOTSETUPFRONTENDACTION_H

#include <clang/Frontend/FrontendAction.h>
#include <clang/AST/ASTConsumer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/StringRef.h>
#include <memory>

class RootSetupFrontendAction : public clang::ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &CI, llvm::StringRef InFile) override;
};

#endif