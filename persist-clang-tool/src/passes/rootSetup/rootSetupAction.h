#ifndef ROOTSETUPACTION_H
#define ROOTSETUPACTION_H

// Clang's entry points for the first half of root-setup. For each
// translation unit the action creates the consumer, and the consumer runs
// RootCollector on the parsed AST. The second half, emitRoots, runs once
// afterwards (main.cc). See ../actionsHelp.md and ../consumersHelp.md.

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>
#include <memory>
#include "rootTable.h"

class RootSetupConsumer : public clang::ASTConsumer {
public:
    explicit RootSetupConsumer(RootTable &table);
    void HandleTranslationUnit(clang::ASTContext &context) override;
private:
    RootTable &table;
};

class RootSetupFrontendAction : public clang::ASTFrontendAction {
public:
    explicit RootSetupFrontendAction(RootTable &table) : table(table) {}
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance &CI, llvm::StringRef InFile) override;
private:
    RootTable &table;
};

// newFrontendActionFactory<T>() can only build actions with no arguments, so
// this factory is what hands every TU's action the same RootTable.
class RootSetupActionFactory : public clang::tooling::FrontendActionFactory {
public:
    explicit RootSetupActionFactory(RootTable &table) : table(table) {}
    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<RootSetupFrontendAction>(table);
    }
private:
    RootTable &table;
};

#endif
