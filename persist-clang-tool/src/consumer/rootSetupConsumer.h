#ifndef ROOTSETUPCONSUMER_H
#define ROOTSETUPCONSUMER_H

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/Support/raw_ostream.h>

class RootSetupConsumer : public clang::ASTConsumer {
public:
    explicit RootSetupConsumer(clang::CompilerInstance &compiler);
    void HandleTranslationUnit(clang::ASTContext &context) override;
private:
    clang::Rewriter rootSetupRewriter;
};

#endif