#ifndef ROOTSETUP_H
#define ROOTSETUP_H

#include <string>
#include <clang/AST/ASTContext.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Basic/SourceLocation.h>
#include <vector>


struct PersistentDecl{
    std::string typeName;
    std::string varName;
    bool isPointer;

};

struct InitSite {
    std::string typeName;
    std::string varName;
    clang::SourceRange newExprRange;
    std::string initArgs;
};

class RootSetup {
    public:
        RootSetup(clang::ASTContext &context, clang::Rewriter &rewriter);
        void start();
    private:
        clang::ASTContext &context;
        clang::Rewriter &rewriter;
        clang::PrintingPolicy printPolicy;
        std::vector<PersistentDecl> persistentDecls;
        std::vector<InitSite> initSites;
        void findPersistentDecls();
        std::string buildRootStruct() const;
        std::string buildInitializers() const;
        clang::SourceLocation findMainLocation();
        void applyRewrites();
        void writeOutput();
};


#endif