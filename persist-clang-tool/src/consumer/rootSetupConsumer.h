#ifndef ROOTSETUPCONSUMER_H
#define ROOTSETUPCONSUMER_H

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include "../transformer/rootSetup.h"

class RootSetupConsumer : public clang::ASTConsumer {
public:
    explicit RootSetupConsumer(RootTable &table);
    void HandleTranslationUnit(clang::ASTContext &context) override;
private:
    RootTable &table;
};

#endif
