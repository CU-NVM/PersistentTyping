#ifndef RECURSEFRONTENDACTION_HPP
#define RECURSEFRONTENDACTION_HPP

#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/StringRef.h>
#include <memory>

#include "../consumer/recurseConsumer.h"

// Clang calls CreateASTConsumer once per file.
// The consumer owns recurseRewriter directly (Pattern B) — no Rewriter lives here.
class RecurseFrontendAction : public clang::ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) override;
};

#endif
