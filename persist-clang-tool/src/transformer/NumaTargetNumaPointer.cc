
#include "NumaTargetNumaPointer.h"
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
#include <sstream>
#include <string>
#include <unordered_map>
#include <map>

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"



NumaTargetNumaPointer::NumaTargetNumaPointer(clang::ASTContext &context, clang::Rewriter &rewriter)
    : Transformer(context, rewriter), RecursiveNumaTyperobj(context, rewriter)
{
    // RecursiveNumaTyperobj(context, rewriter);
    RecursiveNumaTyperobj.addAllSpecializations(&context); 
}
using namespace clang;

void NumaTargetNumaPointer::start(){   
       
    using namespace clang::ast_matchers;
    MatchFinder templateSpecializationFinder;
    // MatchFinder BinaryOperatorFinder;
    auto templateSpecializationDeclMatcher = classTemplateSpecializationDecl().bind("templateSpecializationDecl");
    // auto binaryOperatorMatcher = binaryOperator().bind("assignOp");
    templateSpecializationFinder.addMatcher(templateSpecializationDeclMatcher, this);
    // BinaryOperatorFinder.addMatcher(binaryOperatorMatcher, this);
    // BinaryOperatorFinder.matchAST(context);
    templateSpecializationFinder.matchAST(context);
    return;
}

void NumaTargetNumaPointer::introspectMethods(CXXMethodDecl* method,const clang::CXXRecordDecl* FoundType, llvm::APSInt FoundInt,  const clang::ast_matchers::MatchFinder::MatchResult &result, RecursiveNumaTyper* RecursiveNumaTyper){
    utils::CXXNewExprVisitor CXXNewExprVisitor(result.Context);
    utils::CompoundStmtVisitor CompoundStmtVisitor(result.Context);
    utils::DeclStmtVisitor DeclStmtVisitor(result.Context);
    utils::VarDeclVisitor VarDeclVisitor(result.Context);
    utils::AssignmentOperatorCallVisitor AssignmentOperatorCallVisitor(result.Context);
    utils::MemberExprVisitor MemberExprVisitor(result.Context);

    if(AssignmentOperatorCallVisitor.TraverseStmt(method->getBody())){
        for(auto assignment : AssignmentOperatorCallVisitor.getAssignmentOperatorCallExprs()){
            if(MemberExprVisitor.TraverseStmt(assignment)){
                for(auto memberExpr : MemberExprVisitor.getMemberExprs()){
                    if(memberExpr){
                        if(CXXNewExprVisitor.TraverseStmt(assignment)){
                            for(auto CXXNewExpr : CXXNewExprVisitor.getCXXNewExprs()){
                                if(CXXNewExpr){
                                    llvm::outs()<<"KIDUS KIDUS KIDUS numa type to be casted is "<< CXXNewExpr->getType().getAsString() <<"\n";
                                    llvm::outs()<<"This new expression comes from a member variable assignment. Now calling castNewExprOCE\n";
                                    castNewExprOCE(CXXNewExpr, memberExpr,  FoundType, FoundInt, rewriteLocation, result, RecursiveNumaTyper);
                                }
                                CXXNewExprVisitor.clearCXXNewExprs();
                            }
                        }
                    }
                MemberExprVisitor.clearMemberExprs();
                } 
            }
            AssignmentOperatorCallVisitor.clearAssignmentOperatorCallExprs();
        }
    }

    if(DeclStmtVisitor.TraverseStmt(method->getBody())){
        //llvm::outs()<<"DeclStmt found\n";
        for(auto declStmt : DeclStmtVisitor.getDeclStmts()){
            if(VarDeclVisitor.TraverseStmt(declStmt)){
                for(auto varDecl : VarDeclVisitor.getVarDecls()){
                    if(varDecl){
                        if(CXXNewExprVisitor.TraverseStmt(declStmt)){
                            for(auto CXXNewExpr : CXXNewExprVisitor.getCXXNewExprs()){
                                if(CXXNewExpr){
                                    llvm::outs()<<"KIDUS KIDUS KIDUS numa type to be casted is "<< CXXNewExpr->getType().getAsString() <<"\n";
                                    castNewExprDecls(CXXNewExpr, varDecl,  FoundType, FoundInt, rewriteLocation, result, RecursiveNumaTyper);
                                }
                                CXXNewExprVisitor.clearCXXNewExprs();
                            }
                        }
                    }
                }
                VarDeclVisitor.clearVarDecls();
            }
        }
    }


}

void NumaTargetNumaPointer::castNewExprOCE(clang::CXXNewExpr* CXXNewExpr, clang::MemberExpr* CXXMemberCallExpr , const clang::CXXRecordDecl* FoundType, llvm::APSInt FoundInt,  clang::SourceLocation rewriteLocation, const clang::ast_matchers::MatchFinder::MatchResult &result, RecursiveNumaTyper* RecursiveNumaTyper){
    // llvm::outs()<<"FOUND TYPE FROM CASTNEWEXPR: "<<FoundType->getNameAsString()<<"\n";
    std::string NewType = CXXNewExpr->getAllocatedType().getAsString();
    llvm::outs() << "New Type: " << NewType << "\n";
    llvm::outs() << "MemberExpr Type: " << CXXMemberCallExpr->getMemberDecl()->getType().getAsString() << "\n";
    std::string MemberExprType = CXXMemberCallExpr->getMemberDecl()->getType().getAsString();

    //if the first four letters are numa, then return
    if(MemberExprType.substr(0,4).compare("numa") == 0){
        if(!NewType.substr(0,4).compare("numa") == 0){
            llvm::outs() << "MemberExpr Type is numa but NewType is not\n";
            rewriteLocation = CXXNewExpr->getBeginLoc();
            SourceLocation TypeLoc = CXXNewExpr->getAllocatedTypeSourceInfo()->getTypeLoc().getBeginLoc();
            rewriter.ReplaceText(TypeLoc, NewType.length(), "numa<"+NewType+","+std::to_string(FoundInt.getExtValue())+">");

            rewriter.InsertTextBefore(rewriteLocation, "reinterpret_cast<"+NewType+"*>(");

            //getFIle ID 
            clang::SourceLocation NewEndLocaiton = CXXNewExpr->getEndLoc();
            rewriter.InsertTextAfter(NewEndLocaiton, ")");
            //getFIle ID        
            llvm::outs()<<"Type replaced\n";
            const clang::CXXRecordDecl* NewTypeAsCXXRecodDecl;
            if(const RecordType* RT = CXXNewExpr->getAllocatedType()->getAs<RecordType>()){
                if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                    NewTypeAsCXXRecodDecl = CXXRD;
                    if(RecursiveNumaTyperobj.NumaSpeclExists(NewTypeAsCXXRecodDecl, FoundInt.getExtValue())){
                        llvm::outs() <<  "Specialization for " << NewTypeAsCXXRecodDecl->getNameAsString() << " and " << FoundInt.getExtValue() << " already exists.\n";
                        return;
                    }else{
                        llvm::outs() << "Specializing " << NewTypeAsCXXRecodDecl->getNameAsString() << " and " << FoundInt.getExtValue() << "\n";
                        RecursiveNumaTyperobj.specializeClass(result.Context, NewTypeAsCXXRecodDecl, FoundInt.getExtValue());
                        fileIDs.push_back(rewriter.getSourceMgr().getFileID(rewriteLocation)); 
                    }


                }
            }
           
        }
    }
}

void NumaTargetNumaPointer::castNewExprDecls(clang::CXXNewExpr* CXXNewExpr, clang::VarDecl* VarDecl, const clang::CXXRecordDecl* FoundType, llvm::APSInt FoundInt,  clang::SourceLocation rewriteLocation, const clang::ast_matchers::MatchFinder::MatchResult &result, RecursiveNumaTyper* RecursiveNumaTyper){
    std::string NewType = CXXNewExpr->getAllocatedType().getAsString();
    std::string VarDeclType = VarDecl->getType().getAsString();
    llvm::outs() << "New Type: " << NewType << "\n";
    llvm::outs() << "VarDecl Type: " << VarDeclType << "\n";
    //if the first four letters are numa, then return
    if(!NewType.substr(0,4).compare("numa") == 0){
        //check if dump is empty
        rewriteLocation = CXXNewExpr->getBeginLoc();
        SourceLocation TypeLoc = CXXNewExpr->getAllocatedTypeSourceInfo()->getTypeLoc().getBeginLoc();
        rewriter.ReplaceText(TypeLoc, NewType.length(), "numa<"+NewType+","+std::to_string(FoundInt.getExtValue())+">");

        rewriter.InsertTextBefore(rewriteLocation, "reinterpret_cast<"+NewType+"*>(");

        //getFIle ID 
        clang::SourceLocation NewEndLocaiton = CXXNewExpr->getEndLoc();
        rewriter.InsertTextAfter(NewEndLocaiton, ")");
        const clang::CXXRecordDecl* NewTypeAsCXXRecodDecl;
        if(const RecordType* RT = CXXNewExpr->getAllocatedType()->getAs<RecordType>()){
            if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                NewTypeAsCXXRecodDecl = CXXRD;
                if(RecursiveNumaTyperobj.NumaSpeclExists(NewTypeAsCXXRecodDecl, FoundInt.getExtValue())){
                    llvm::outs() <<  "Specialization for " << NewTypeAsCXXRecodDecl->getNameAsString() << " and " << FoundInt.getExtValue() << " already exists.\n";
                    return;
                }else{
                    llvm::outs() << "Specializing " << NewTypeAsCXXRecodDecl->getNameAsString() << " and " << FoundInt.getExtValue() << "\n";
                    RecursiveNumaTyperobj.specializeClass(result.Context, NewTypeAsCXXRecodDecl, FoundInt.getExtValue());
                    fileIDs.push_back(rewriter.getSourceMgr().getFileID(rewriteLocation)); 
                }
            }
        }
    }
}


void NumaTargetNumaPointer::run(const clang::ast_matchers::MatchFinder::MatchResult &result){

    if(result.SourceManager->isInSystemHeader(result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getSourceRange().getBegin()))
        return;
    if(result.SourceManager->getFilename(result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getLocation()).find("../numaLib/numatype.hpp") != std::string::npos)
        return;
    if(result.SourceManager->getFilename(result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getLocation()).find("../numaLib/numathreads.hpp") != std::string::npos)
        return;
    if(result.SourceManager->getFilename(result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getLocation()).empty())
        return;
    if(result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->isImplicit())
        return;
    if(result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getNameAsString().empty())
        return;



    for(auto &specialization : RecursiveNumaTyperobj.getSpecializedClasses()){
        llvm::outs() << "Specialized Class: " << specialization.first->getNameAsString() << " NodeID: " << specialization.second << "\n";
    }

    // const BinaryOperator *AssignOp = result.Nodes.getNodeAs<BinaryOperator>("assignOp"); 
    //     //print the binary operator
    // if(AssignOp){
    // llvm::outs() << "Found Binary Operator: " << AssignOp->getOpcodeStr() << "\n";
    // }


    const auto *TemplateDecl = result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getSpecializedTemplate();
            //check if its numa
    const CXXRecordDecl* FoundType;
    llvm::APSInt FoundInt;
    llvm::outs() << "The template class name is " << TemplateDecl->getNameAsString() << "\n";

    if(TemplateDecl->getNameAsString().compare("numa") == 0){

        for (const auto &Arg : result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->getTemplateArgs().asArray()) {
            if (Arg.getKind() == clang::TemplateArgument::ArgKind::Type) {
                if(const RecordType *RT = Arg.getAsType()->getAs<RecordType>()){
                    if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                        llvm::outs() << "Found CXXRecordDecl: " << CXXRD->getNameAsString() << "\n";
                        FoundType = CXXRD;
                    }
                }
            }
            if (Arg.getKind() == clang::TemplateArgument::ArgKind::Integral) {
                llvm::outs() << "The integral is " << Arg.getAsIntegral() << "\n";
                FoundInt = Arg.getAsIntegral();
                for(auto method : result.Nodes.getNodeAs<ClassTemplateSpecializationDecl>("templateSpecializationDecl")->methods()){
                    llvm::outs()<< "Gonna introspect the method "<<method->getNameAsString() <<" of the class " << FoundType->getNameAsString() << " that is specialized in numa class\n";
                    if (method->hasBody()) { // Check if the method has a body
                            //look for CXXnewExpr in the method body
                        introspectMethods(method, FoundType, FoundInt, result, &RecursiveNumaTyperobj);
                    }
                }
            }
        
            
        }
    }
    return;
}

void NumaTargetNumaPointer::print(clang::raw_ostream &stream){

}