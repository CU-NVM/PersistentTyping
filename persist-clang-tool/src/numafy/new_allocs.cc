#include "new_allocs.h"
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
#include <sstream>
#include <string>
#include <unordered_map>
#include <map>

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"



using namespace clang;

std::vector<CXXNewExpr*> getNewExprFromMethods(const clang::ASTContext& Context, CXXMethodDecl* method){
    std::vector<CXXNewExpr*> newExprs;
    utils::CXXNewExprVisitor CXXNewExprVisitor(&Context);
    utils::CompoundStmtVisitor CompoundStmtVisitor(&Context);
    utils::DeclStmtVisitor DeclStmtVisitor(&Context);
    utils::VarDeclVisitor VarDeclVisitor(&Context);
    utils::AssignmentOperatorCallVisitor AssignmentOperatorCallVisitor(&Context);
    utils::MemberExprVisitor MemberExprVisitor(&Context);

    if(AssignmentOperatorCallVisitor.TraverseStmt(method->getBody())){
        for(auto assignment : AssignmentOperatorCallVisitor.getAssignmentOperatorCallExprs()){
            if(MemberExprVisitor.TraverseStmt(assignment)){
                for(auto memberExpr : MemberExprVisitor.getMemberExprs()){
                    if(memberExpr){
                        //printout member expr
                        if(CXXNewExprVisitor.TraverseStmt(assignment)){
                            for(auto CXXNewExpr : CXXNewExprVisitor.getCXXNewExprs()){
                                if(CXXNewExpr){
                                    newExprs.push_back(CXXNewExpr);
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
                                    newExprs.push_back(CXXNewExpr);
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
    return newExprs;
}