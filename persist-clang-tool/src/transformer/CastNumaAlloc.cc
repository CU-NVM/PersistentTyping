
#include "CastNumaAlloc.h"
#include "../numafy/new_allocs.h"
#include "../utils/utils.h"
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <clang/Rewrite/Core/Rewriter.h>
#include <string>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/IRReader/IRReader.h"
#include "RecursiveNumaTyper.h"
#include "../casting/reinterprete_cast.h"
#include <sstream>
#include <string>
#include <unordered_map>
#include <map>

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang;

CastNumaAlloc::CastNumaAlloc(clang::ASTContext &context, clang::Rewriter &rewriter)
    : Transformer(context, rewriter), RecursiveNumaTyperobj(context, rewriter)
{
    // RecursiveNumaTyperobj(context, rewriter);
    RecursiveNumaTyperobj.addAllSpecializations(context); 
}

void CastNumaAlloc::start(){   
       
    using namespace clang::ast_matchers;
    MatchFinder templateSpecializationFinder;

    auto templateSpecializationDeclMatcher = classTemplateSpecializationDecl(unless(isExpansionInSystemHeader()),
                                    unless(isExpansionInFileMatching("numatype.hpp")),
                                    unless(isExpansionInFileMatching("numathreads.hpp")),
                                    unless(isExpansionInFileMatching(".*/umf/.*"))).bind("templateSpecializationDecl");
    templateSpecializationFinder.addMatcher(templateSpecializationDeclMatcher, this);
    templateSpecializationFinder.matchAST(context);
    //printCandidateTemplateSpecializations(context);

    for(auto templateSpecDecl : numaTemplateSpecializations){
        const CXXRecordDecl* FoundType;
        llvm::APSInt FoundInt;
        for (const auto &Arg : templateSpecDecl->getTemplateArgs().asArray()) {
            if (Arg.getKind() == clang::TemplateArgument::ArgKind::Type) {
                if(const RecordType *RT = Arg.getAsType()->getAs<RecordType>()){
                    if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                        FoundType = CXXRD;
                    }
                }
            }
            if (Arg.getKind() == clang::TemplateArgument::ArgKind::Integral) {
               
                FoundInt = Arg.getAsIntegral();
                for(auto method : templateSpecDecl->methods()){
                
                    if (method->hasBody()) { // Check if the method has a body
                            //look for CXXnewExpr in the method body
                        introspectMethods(method, FoundType, FoundInt, context, &RecursiveNumaTyperobj);
                    }
                }
            }
        }

    }
    return;
}

void CastNumaAlloc::introspectMethods(CXXMethodDecl* method,const clang::CXXRecordDecl* FoundType, llvm::APSInt FoundInt,  clang::ASTContext &context, RecursiveNumaTyper* RecursiveNumaTyper){
    std::vector<CXXNewExpr*> newExprsInBody = getNewExprFromMethods(context, method);
    for(auto newExpr : newExprsInBody){
        //get member expr from the new expr
        castNewExprOCE(newExpr,  FoundType, FoundInt, rewriteLocation, context, RecursiveNumaTyper);
    }
}

void CastNumaAlloc::castNewExprOCE(clang::CXXNewExpr* CXXNewExpr , const clang::CXXRecordDecl* FoundType, llvm::APSInt FoundInt,  clang::SourceLocation rewriteLocation, clang::ASTContext &context, RecursiveNumaTyper* RecursiveNumaTyper){
    // llvm::outs()<<"FOUND TYPE FROM CASTNEWEXPR: "<<FoundType->getNameAsString()<<"\n";
    std::string NewType = CXXNewExpr->getAllocatedType().getAsString();
    std::string CastType = CXXNewExpr->getType().getAsString();
    if(NewType.substr(0,4).compare("numa") != 0){
        auto &SM = rewriter.getSourceMgr();
        auto &LO = rewriter.getLangOpts();
        numafyAndReinterpreteCast(rewriter, SM, LO, CXXNewExpr, FoundInt);
    }
}

void CastNumaAlloc::run(const clang::ast_matchers::MatchFinder::MatchResult &result){
    if(const auto *TD = result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")){
        //check if the specialization is numa
        if(TD->getSpecializedTemplate()->getNameAsString().compare("numa") == 0){
            numaTemplateSpecializations.push_back(TD);
        }
    }

    return;
}

void CastNumaAlloc::printCandidateTemplateSpecializations(const clang::ASTContext& context){
    llvm::outs() << "================= Printing all NUMA template specializations =================\n";

    for(auto templateSpec : numaTemplateSpecializations){
        //pretty print the new expression
        templateSpec->print(llvm::outs());
        llvm::outs() << "\n";
        llvm::outs() << "Type of NUMA template specialization is "<< templateSpec->getNameAsString() << "\n";
    }
    llvm::outs() << "Found " << numaTemplateSpecializations.size() << " numa template specializations\n";
    return;
}

void CastNumaAlloc::print(llvm::raw_ostream &stream){

}