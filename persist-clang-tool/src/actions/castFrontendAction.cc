#include "castFrontendAction.h"
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include "clang/Lex/Preprocessor.h"
#include "../consumer/castConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/AST/AST.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Basic/SourceManager.h"



std::unique_ptr<clang::ASTConsumer> CastFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile)
{


    llvm::errs() << "** Creating Consumer from CastFrontendAction **" << inFile << "\n";
    TheRewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    // llvm::outs() << "file hash value: " << TheRewriter.getSourceMgr().getMainFileID().getHashValue() << "\n";

    return std::make_unique<CastConsumer>(TheRewriter, &compiler.getASTContext());
}

