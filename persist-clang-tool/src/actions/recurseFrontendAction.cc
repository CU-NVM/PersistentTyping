#include "recurseFrontendAction.h"
#include "../consumer/recurseConsumer.h"

std::unique_ptr<clang::ASTConsumer>
RecurseFrontendAction::CreateASTConsumer(
    clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "Running RecursePersistentFrontendAction on file: " << inFile << "\n";
    return std::make_unique<RecurseConsumer>(compiler);
}