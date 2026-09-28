#ifndef RECURSECONSUMER_HPP
#define RECURSECONSUMER_HPP
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Rewrite/Core/Rewriter.h>

class RecurseConsumer : public clang::ASTConsumer {
public:
    explicit RecurseConsumer(clang::CompilerInstance &compiler);
    void HandleTranslationUnit(clang::ASTContext &context) override;

private:
    clang::Rewriter rewriter;
};


#endif
