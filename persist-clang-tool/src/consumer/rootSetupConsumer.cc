#include "rootSetupConsumer.h"
#include "../transformer/rootSetup.h"
RootSetupConsumer::RootSetupConsumer(clang::CompilerInstance &compiler){
    rootSetupRewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
}

void RootSetupConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    llvm::outs() << "Handling translation unit in RootSetupConsumer\n";     
    RootSetup rootSetup(context, rootSetupRewriter);
    rootSetup.start();
}   