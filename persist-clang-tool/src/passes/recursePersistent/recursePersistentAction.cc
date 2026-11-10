#include "recursePersistentAction.h"
#include "specializationBuilder.h"
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/Support/raw_ostream.h>

RecursePersistentConsumer::RecursePersistentConsumer(clang::CompilerInstance &compiler) {
    rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
}

void RecursePersistentConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    llvm::outs() << "Handling translation unit in RecursePersistentConsumer\n";
    SpecializationBuilder builder(context, rewriter);
    builder.run();
}

std::unique_ptr<clang::ASTConsumer>
RecursePersistentFrontendAction::CreateASTConsumer(
    clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    llvm::errs() << "Running RecursePersistentFrontendAction on file: " << inFile << "\n";
    return std::make_unique<RecursePersistentConsumer>(compiler);
}
