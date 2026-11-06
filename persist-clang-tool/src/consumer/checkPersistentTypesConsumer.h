#ifndef CHECKPERSISTENTTYPESCONSUMER_H
#define CHECKPERSISTENTTYPESCONSUMER_H

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include "../transformer/checkPersistentTypes.h"

class CheckPersistentTypesConsumer : public clang::ASTConsumer {
public:
    explicit CheckPersistentTypesConsumer(CheckPersistentTypesTable &table);
    void HandleTranslationUnit(clang::ASTContext &context) override;
private:
    CheckPersistentTypesTable &table;
};

#endif
