#include "consumer.h"
#include "../transformer/RecursiveNumaTyper.h"
#include <fstream>
#include <string>
#include "llvm/Support/WithColor.h"
#include <cstdlib>
NumaConsumer::NumaConsumer(clang::Rewriter& TheReWriter, clang::ASTContext* context)
{
    rewriter = TheReWriter;
}

void NumaConsumer::WriteOutput(clang::SourceManager &SM){
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
                if(it->first.getName().find("include/umf/") != std::string::npos){
                    llvm::outs() << "Skipping include/umf/\n";
                    continue;
                }


                std::string fileName = (std::string)it->first.getName();
                //llvm::outs()<<"WORKNEH FILE TO BE WRITTEN: "<<fileName<<"\n";
                std::string outputFileName = fileName.replace(fileName.find("input"), 5, "output");
               // llvm::outs() << "Output File Name: " << outputFileName << "\n";
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
void NumaConsumer::includeNumaHeader(clang::ASTContext &context){
    for(auto it = rewriter.getSourceMgr().fileinfo_begin(); it != rewriter.getSourceMgr().fileinfo_end(); it++){
        const FileEntry *FE = it->first;
        if(FE){
            FileID FID= context.getSourceManager().getOrCreateFileID(it->first, SrcMgr::CharacteristicKind::C_User);
            //print all the file names
            //llvm::outs() << "FROM INCLUDE NUMA HEADER File Name: " << it->first.getName() << "\n";
            //skip if file name is testsuite.hpp
            std::string home = std::getenv("HOME");
            if(it->first.getName().find("TestSuite.hpp") != std::string::npos){
                llvm::outs() << "Skipping :" << it->first.getName() << "\n";
                continue;
            }
            if(it->first.getName().find("umf/") != std::string::npos){
                    llvm::outs() << "Skipping include/umf/\n";
                    continue;
                }
            if(it->first.getName().find("umf_numa_allocator.hpp") != std::string::npos){
                llvm::outs() << "Skipping umf_numa_allocator.hpp\n";
                continue;
            }

            std::string includeheaders = R"(#ifdef UMF 
	                #include "numatype.hpp"
	                #include <umf/mempolicy.h>
	                #include <umf/memspace.h>
                    #include "utils_examples.h"
                    #include "umf_numa_allocator.hpp"
                    #include <numa.h>
                    #include <numaif.h>
                    #include <stdio.h>
                    #include <string.h>
                #endif
                #include "numatype.hpp"
                )";

            // Skip invalid or built-in files
            if (FID.isInvalid() || FID == context.getSourceManager().getMainFileID())
            continue;

            // Insert the include directive at the beginning of each file
            rewriter.InsertTextBefore( context.getSourceManager().getLocForStartOfFile(FID),includeheaders);

        }
    }
}


void NumaConsumer::HandleTranslationUnit(clang::ASTContext &context){
    llvm::outs() <<"Calling template arg transformer\n";
    RecursiveNumaTyper recursiveNumaTyper(context, rewriter);
    // // //fntransformer.start();
    recursiveNumaTyper.start();
    //fntransformer.print(llvm::outs());   
    llvm::outs() << "Get all the file names in rewriter source manager\n";
    for(auto it = rewriter.getSourceMgr().fileinfo_begin(); it != rewriter.getSourceMgr().fileinfo_end(); it++){
        rewriterFileNames.push_back( it->first.getName());
    }

    includeNumaHeader(context);       //turn this on to include numaheaders
    WriteOutput(rewriter.getSourceMgr());
}
    




