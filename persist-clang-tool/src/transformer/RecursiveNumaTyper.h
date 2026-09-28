#ifndef RECURSIVENUMATYPER_HPP
#define RECURSIVENUMATYPER_HPP

#include "transformer.h"
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include <sstream>
#include <string>
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <unordered_set>
#include <set>
#include <map>

using namespace clang;
class RecursiveNumaTyper : public Transformer
{
    private:
        std::vector<std::pair<const clang::CXXRecordDecl*, llvm::APInt>> specializedClasses;
        clang::SourceLocation rewriteLocation;
        std::vector<clang::FileID> fileIDs;
        std::vector<const clang::CXXNewExpr*> newNumaExprs;
        std::unordered_set<std::string> seenNewNumaTypes;
        std::vector<const clang::VarDecl*> numaVarDecls;
        std::unordered_set<std::string> seenVarDeclTypes;

        
    public:
        
        explicit RecursiveNumaTyper(clang::ASTContext &context, clang::Rewriter &rewriter);

        virtual void start() override;
        virtual void print(clang::raw_ostream &stream) override;
        virtual void run(const clang::ast_matchers::MatchFinder::MatchResult &result)override;

        bool NumaDeclExists(const clang::ASTContext &Context, QualType FirstTemplateArg, llvm::APInt SecondTemplateArg);
        void addAllSpecializations(const clang::ASTContext &Context);
        std::vector<std::pair<const clang::CXXRecordDecl*, llvm::APInt>> getSpecializedClasses(){
            return specializedClasses;
        }
        bool NumaSpeclExists(const clang::CXXRecordDecl* FirstTemplateArg, llvm::APInt SecondTemplateArg);
        void makeVirtual(const clang::CXXRecordDecl *classDecl);

        void specializeClass(const clang::ASTContext& Context, const clang::CXXRecordDecl* FirstTemplateArg, llvm::APInt SecondTemplateArg);
        void constructSpecialization(const clang::ASTContext& Context,const clang::CXXRecordDecl* classDecl, llvm::APInt nodeID);

        void numaPublicMembers(const clang::ASTContext& Context, clang::SourceLocation& rewriteLocation, std::vector<clang::FieldDecl*> publicFields,std::vector<clang::CXXMethodDecl*> publicMethods, llvm::APInt nodeID);
        void numaPrivateMembers(const clang::ASTContext& Context, clang::SourceLocation& rewriteLocation,std::vector<clang::FieldDecl*> privateFields, std::vector<clang::CXXMethodDecl*> privateMethods, llvm::APInt nodeID);

        void numaConstructors(clang::CXXConstructorDecl* Ctor, clang::SourceLocation& rewriteLocation, llvm::APInt nodeID);
        std::string getNumaConstructorSignature(clang::CXXConstructorDecl* Ctor);

        void numaDestructors(clang::CXXDestructorDecl* Dtor, clang::SourceLocation& rewriteLocation, llvm::APInt nodeID);
        //TODO: Refactor numaDestructors according to numaConstructors
        void numaMethods(const clang::ASTContext& Context, clang::CXXMethodDecl* method, clang::SourceLocation& rewriteLocation, llvm::APInt nodeID);
        std::string getNumaMethodSignature(clang::CXXMethodDecl* method);
        void printNumaCandidates(const clang::ASTContext& context);
        
};





#endif
