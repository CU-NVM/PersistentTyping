#include "rootSetupAction.h"
#include "rootCollector.h"
#include <llvm/Support/raw_ostream.h>

RootSetupConsumer::RootSetupConsumer(RootTable &table) : table(table) {}

void RootSetupConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    RootCollector collector(context, table);
    collector.run();
}

std::unique_ptr<clang::ASTConsumer>
RootSetupFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "Running RootSetupFrontendAction on file: " << inFile << "\n";
    return std::make_unique<RootSetupConsumer>(table);
}
