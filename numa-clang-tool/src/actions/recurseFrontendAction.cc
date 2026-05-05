#include "recurseFrontendAction.h"
#include "../consumer/recurseConsumer.h"

std::unique_ptr<clang::ASTConsumer>
RecurseFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "** RecurseFrontendAction: processing " << inFile << "\n";
    // Pass the full compiler instance so the consumer can init its own Rewriter
    return std::make_unique<RecurseConsumer>(compiler);
}
