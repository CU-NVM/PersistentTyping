#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "inclusiondirective.h"

using namespace clang;

void IncludeTracker::InclusionDirective(SourceLocation HashLoc,
                                  const Token &IncludeTok, StringRef FileName,
                                  bool IsAngled, CharSourceRange FilenameRange,
                                  OptionalFileEntryRef File,
                                  StringRef SearchPath, StringRef RelativePath,
                                  const Module *Imported,
                                  SrcMgr::CharacteristicKind FileType){

        // if(std::find(exceptionIncludes.begin(), exceptionIncludes.end(), FileName.str()) != exceptionIncludes.end())
        // {
        //     return;
        // }        
        // std::string NewFileName = "\"out_" + FileName.str() + "\"";
        // TheRewriter.ReplaceText(FilenameRange, NewFileName);

        // //get location of rewriter
        // rewriteLoc = FilenameRange.getBegin();
        // llvm::StringRef file_name = TheRewriter.getSourceMgr().getFilename(rewriteLoc);
        // //add "out_" to the file name that is after the last /
        // std::string output_file = "out_" + file_name.rsplit('/').second.str();
        // std::error_code error_code;
        // llvm::raw_fd_ostream outFile("../output/"+output_file, error_code);
        // TheRewriter.buffer_begin()->second.write(outFile);

        if(File){
            //includedFiles.push_back(File->getName());
            includedFilesSet.push_back(File->getName());
            // llvm::outs() << File->getName() << "\n";
        }


}


