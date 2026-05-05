#include "recurseConsumer.h"
#include "../transformer/RecursiveNumaTyper.h"
#include <fstream>
#include <string>
#include "llvm/Support/WithColor.h"
#include <cstdlib>

RecurseConsumer::RecurseConsumer(clang::CompilerInstance &compiler) {
    // Consumer owns and initializes the rewriter directly (no copy from action)
    recurseRewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
}

void RecurseConsumer::WriteOutput(clang::SourceManager &SM) {
    for (auto it = SM.fileinfo_begin(); it != SM.fileinfo_end(); it++) {
        const FileEntry *FE = it->first;

        if (FE) {
            FileID FID = SM.getOrCreateFileID(it->first, SrcMgr::CharacteristicKind::C_User);
            auto buffer = recurseRewriter.getRewriteBufferFor(FID);
            if (buffer) {
                SourceLocation Loc = SM.getLocForStartOfFile(FID);

                if (SM.isInSystemHeader(Loc))
                    continue;
                if (it->first.getName().find("numaLib/numatype.hpp") != std::string::npos)
                    continue;
                if (it->first.getName().find("numaLib/numathreads.hpp") != std::string::npos)
                    continue;
                if (it->first.getName().find("include/umf/") != std::string::npos)
                    continue;

                std::string fileName = (std::string)it->first.getName();
                std::string outputFileName = fileName.replace(fileName.find("input"), 5, "output");

                std::string directory = outputFileName.substr(0, outputFileName.find_last_of("/"));
                system(("mkdir -p " + directory).c_str());

                std::error_code EC;
                llvm::raw_fd_ostream OutFile((llvm::StringRef)outputFileName, EC, llvm::sys::fs::OF_Text);
                if (EC) {
                    llvm::errs() << "Error opening output file: " << EC.message() << "\n";
                    return;
                }
                buffer->write(OutFile);
            }
        }
    }
}



void RecurseConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    llvm::outs() << "Calling RecursiveNumaTyper from RecurseConsumer\n";
    RecursiveNumaTyper recursiveNumaTyper(context, recurseRewriter);
    recursiveNumaTyper.start();

    for (auto it = recurseRewriter.getSourceMgr().fileinfo_begin();
         it != recurseRewriter.getSourceMgr().fileinfo_end(); it++) {
        rewriterFileNames.push_back(it->first.getName());
    }
    WriteOutput(recurseRewriter.getSourceMgr());
}
