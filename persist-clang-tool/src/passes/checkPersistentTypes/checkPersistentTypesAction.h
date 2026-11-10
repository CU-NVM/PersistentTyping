#ifndef CHECKPERSISTENTTYPESACTION_H
#define CHECKPERSISTENTTYPESACTION_H

// Clang's entry points for the check-persistent-types pass. For each
// translation unit the action creates the consumer, and the consumer runs
// CheckPersistentTypesCollector on the parsed AST. See ../actionsHelp.md and
// ../consumersHelp.md.

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>
#include <memory>
#include "checkPersistentTypes.h"

class CheckPersistentTypesConsumer : public clang::ASTConsumer {
public:
    explicit CheckPersistentTypesConsumer(CheckPersistentTypesTable &table);
    void HandleTranslationUnit(clang::ASTContext &context) override;
private:
    CheckPersistentTypesTable &table;
};

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
