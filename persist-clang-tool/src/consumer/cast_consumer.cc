#include "cast_consumer.h"
#include "../transformer/NumaTargetNumaPointer.h"
#include <fstream>
#include <string>
#include "llvm/Support/WithColor.h"
#include "clang/CodeGen/CodeGenAction.h"
// #include "clang/lib/CodeGen/CodeGenModule.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/InitLLVM.h"

CastConsumer::CastConsumer(clang::Rewriter& TheReWriter, clang::ASTContext* context)
{
    rewriter = TheReWriter;
}

void CastConsumer::WriteOutput(clang::SourceManager &SM){
    for(auto it = SM.fileinfo_begin(); it != SM.fileinfo_end(); it++){
        const FileEntry *FE = it->first;

        if(FE){
            FileID FID= SM.getOrCreateFileID(it->first, SrcMgr::CharacteristicKind::C_User);
            auto buffer = rewriter.getRewriteBufferFor(FID);
            if(buffer){
                // llvm::outs() << "File ID: " << FID.getHashValue() << " File Name: " << it->first.getName() << "\n";
                //change /input to /output in File Name
                SourceLocation Loc = SM.getLocForStartOfFile(FID);
                
                if(SM.isInSystemHeader(Loc)){
                    // llvm::outs() << "Skipping system header\n";
                    continue;
                }

                if(it->first.getName().find("numaLib/numatype.hpp") != std::string::npos){
                    // llvm::outs() << "Skipping numaLib/numatype.hpp\n";
                    continue;
                }

                if(it->first.getName().find("numaLib/numathreads.hpp") != std::string::npos){
                    // llvm::outs() << "Skipping numaLib/numatype.hpp\n";
                    continue;
                }

                std::string fileName = (std::string)it->first.getName();
                std::string outputFileName = fileName.replace(fileName.find("output"), 6, "output2");
                llvm::outs() << "Output File Name: " << outputFileName << "\n";
                //if directory doesn't exist create it
            
                std::string directory = outputFileName.substr(0, outputFileName.find_last_of("/"));
                std::string command = "mkdir -p " + directory;  
                system(command.c_str());

                std::error_code EC;
                llvm::raw_fd_ostream OutFile((llvm::StringRef)outputFileName, EC, llvm::sys::fs::OF_Text);
                if(EC){
                    llvm::errs() << "Error opening output file: " << EC.message() << "\n";
                    return;
                }
                buffer->write(OutFile);
                //write buffer
                // buffer->write(llvm::outs());
            }
        }
    }
}
void CastConsumer::includeNumaHeader(clang::ASTContext &context){
    for(auto it = rewriter.getSourceMgr().fileinfo_begin(); it != rewriter.getSourceMgr().fileinfo_end(); it++){
        const FileEntry *FE = it->first;
        if(FE){
            FileID FID= context.getSourceManager().getOrCreateFileID(it->first, SrcMgr::CharacteristicKind::C_User);
    

            // Skip invalid or built-in files
            if (FID.isInvalid() || FID == context.getSourceManager().getMainFileID())
            continue;

            // Insert the include directive at the beginning of each file
            rewriter.InsertTextBefore( context.getSourceManager().getLocForStartOfFile(FID),"#include \"numatype.hpp\"\n");
        }
    }
}


void CastConsumer::HandleTranslationUnit(clang::ASTContext &context){
    llvm::outs() <<"Calling template arg transformer from cast consumer\n";
    NumaTargetNumaPointer numaTargetNumaPointer(context, rewriter);
    // // //fntransformer.start();
    numaTargetNumaPointer.start();
    //fntransformer.print(llvm::outs());   
    llvm::outs() << "Get all the file names in rewriter source manager\n";
    // for(auto it = rewriter.getSourceMgr().fileinfo_begin(); it != rewriter.getSourceMgr().fileinfo_end(); it++){
    //     rewriterFileNames.push_back( it->first.getName());
    // }

    // includeNumaHeader(context);       //turn this on to include numaheaders
    WriteOutput(rewriter.getSourceMgr());
}
    




