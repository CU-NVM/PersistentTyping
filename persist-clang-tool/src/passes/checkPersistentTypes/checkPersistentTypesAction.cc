#include "checkPersistentTypesAction.h"
#include <llvm/Support/raw_ostream.h>

CheckPersistentTypesConsumer::CheckPersistentTypesConsumer(CheckPersistentTypesTable &table) : table(table) {}

void CheckPersistentTypesConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    CheckPersistentTypesCollector collector(context, table);
    collector.run();
}

std::unique_ptr<clang::ASTConsumer>
CheckPersistentTypesFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "Running CheckPersistentTypesFrontendAction on file: " << inFile << "\n";
    return std::make_unique<CheckPersistentTypesConsumer>(table);
}
