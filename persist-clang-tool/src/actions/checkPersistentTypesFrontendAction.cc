#include "checkPersistentTypesFrontendAction.h"
#include "../consumer/checkPersistentTypesConsumer.h"

std::unique_ptr<clang::ASTConsumer>
CheckPersistentTypesFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "Running CheckPersistentTypesFrontendAction on file: " << inFile << "\n";
    return std::make_unique<CheckPersistentTypesConsumer>(table);
}
