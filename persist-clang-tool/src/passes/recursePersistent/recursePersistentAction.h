#ifndef RECURSEPERSISTENTACTION_H
#define RECURSEPERSISTENTACTION_H

// Clang's entry points for the recurse-persistent pass. For each translation
// unit the action creates the consumer, and the consumer runs
// SpecializationBuilder on the parsed AST, which writes the TU's files when it
// is done. See ../actionsHelp.md and ../consumersHelp.md.

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/ADT/StringRef.h>
#include <memory>

namespace clang { class CompilerInstance; }

class RecursePersistentConsumer : public clang::ASTConsumer {
public:
    explicit RecursePersistentConsumer(clang::CompilerInstance &compiler);
    void HandleTranslationUnit(clang::ASTContext &context) override;
private:
    clang::Rewriter rewriter;
};

class RecursePersistentFrontendAction : public clang::ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &CI, llvm::StringRef InFile) override;
};

#endif
