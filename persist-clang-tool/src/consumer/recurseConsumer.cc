#include "recurseConsumer.h"
#include "../transformer/recursePersistentTyper.h"

RecurseConsumer::RecurseConsumer(clang::CompilerInstance &compiler) {
    rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
}

void RecurseConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    llvm::outs() << "Handling translation unit in RecurseConsumer\n";
    RecursePersistentTyper typer(context, rewriter);
    typer.start();
}