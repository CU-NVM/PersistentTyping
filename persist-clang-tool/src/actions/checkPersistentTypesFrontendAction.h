#ifndef CHECKPERSISTENTTYPESFRONTENDACTION_H
#define CHECKPERSISTENTTYPESFRONTENDACTION_H

#include <clang/Frontend/FrontendAction.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>
#include <memory>
#include "../transformer/checkPersistentTypes.h"

class CheckPersistentTypesFrontendAction : public clang::ASTFrontendAction {
public:
    explicit CheckPersistentTypesFrontendAction(CheckPersistentTypesTable &table) : table(table) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &CI, llvm::StringRef InFile) override;
private:
    CheckPersistentTypesTable &table;
};

// newFrontendActionFactory<T>() can only build actions with no arguments, so
// this factory is what hands every TU's action the same CheckPersistentTypesTable.
class CheckPersistentTypesActionFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit CheckPersistentTypesActionFactory(CheckPersistentTypesTable &table) : table(table) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CheckPersistentTypesFrontendAction>(table);
    }
private:
    CheckPersistentTypesTable &table;
};

#endif
