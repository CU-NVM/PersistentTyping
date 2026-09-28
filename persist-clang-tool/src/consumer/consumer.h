#ifndef CONSUMER_HPP
#define CONSUMER_HPP

#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Rewrite/Core/Rewriter.h>

namespace clang
{
    class ASTContext;
}

class NumaConsumer : public clang::ASTConsumer 
{
    private:
        //The rewriter object we use to write the changes in source code
        clang::Rewriter rewriter;
        //The container for the file IDs in the rewriters source manager
        std::vector<llvm::StringRef> rewriterFileNames;
        
    public:
        explicit NumaConsumer(clang::Rewriter& TheReWriter, clang::ASTContext* context);
        void includeNumaHeader(clang::ASTContext &context);
        void WriteOutput(clang::SourceManager &SM);
        virtual void HandleTranslationUnit( clang::ASTContext &context) override;
};
// class PPConsumer : public clang::ASTConsumer 
// {
//     private:
    
//         clang::Rewriter rewriter;

//     public:
//         explicit PPConsumer(clang::Rewriter TheReWriter, clang::ASTContext* context);
//         //PPConsumer is a deadend. The PPFrontend action has already taken care of the include directives.
// };



#endif
