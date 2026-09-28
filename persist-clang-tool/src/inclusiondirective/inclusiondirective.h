#ifndef INCLUSIONDIRECTIVE_HPP
#define INCLUSIONDIRECTIVE_HPP

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

class IncludeTracker : public clang::PPCallbacks {
public:
    IncludeTracker(clang::SourceManager &SM, clang::Rewriter &R)
        : SM(SM), TheRewriter(R) {}

    // void InclusionDirective(SourceLocation HashLoc,
    //                               const Token &IncludeTok, StringRef FileName,
    //                               bool IsAngled, CharSourceRange FilenameRange,
    //                               OptionalFileEntryRef File,
    //                               StringRef SearchPath, StringRef RelativePath,
    //                               const Module *Imported,
    //                               SrcMgr::CharacteristicKind FileType) override;

    std::vector<llvm::StringRef> getIncludedFiles(){ return includedFilesSet; }

private:
    clang::SourceManager &SM;
    clang::Rewriter &TheRewriter;
    clang::SourceLocation rewriteLoc;
    std::vector<std::string> exceptionIncludes = {"numatype.hpp", "numa.h"};
    //std::vector<llvm::StringRef> includedFiles;
    std::vector<llvm::StringRef> includedFilesSet;
};



#endif