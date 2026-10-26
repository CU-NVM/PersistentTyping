#ifndef ROOTSETUPFRONTENDACTION_H
#define ROOTSETUPFRONTENDACTION_H

#include <clang/Frontend/FrontendAction.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringRef.h>
#include <memory>
#include "../transformer/rootSetup.h"

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
