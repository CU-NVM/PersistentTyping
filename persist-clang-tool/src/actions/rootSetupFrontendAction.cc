#include "rootSetupFrontendAction.h"
#include "../consumer/rootSetupConsumer.h"

std::unique_ptr<clang::ASTConsumer> 
RootSetupFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "Running RootSetupFrontendAction on file: " << inFile << "\n";
    return std::make_unique<RootSetupConsumer>(compiler);
}
