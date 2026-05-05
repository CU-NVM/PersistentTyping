#ifndef RECURSECONSUMER_HPP
#define RECURSECONSUMER_HPP

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <vector>

class RecurseConsumer : public clang::ASTConsumer {
public:
    // Takes the full compiler instance so it can initialize recurseRewriter itself
    explicit RecurseConsumer(clang::CompilerInstance &compiler);

    void includeNumaHeader(clang::ASTContext &context);
    void WriteOutput(clang::SourceManager &SM);
    void HandleTranslationUnit(clang::ASTContext &context) override;

private:
    clang::Rewriter recurseRewriter;
    std::vector<llvm::StringRef> rewriterFileNames;
};

#endif
