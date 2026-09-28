#ifndef VISITORS_H
#define VISITORS_H


#include "recursePersistentTyper.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/Support/raw_ostream.h>
#include "../utils/utils.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Lex/Lexer.h>

class NewExprRewriter: public clang::RecursiveASTVisitor<NewExprRewriter> {
public:
    NewExprRewriter(const clang::PrintingPolicy& policy, RecursePersistentTyper* typer,
                    clang::Rewriter& scratch)
        : printPolicy(policy), typer(typer), scratch(scratch) {}
    bool VisitCXXNewExpr(clang::CXXNewExpr* newExpr);
private:
    const clang::PrintingPolicy& printPolicy;
    RecursePersistentTyper* typer;
    clang::Rewriter& scratch;
};

class LocalLHSRewriter : public clang::RecursiveASTVisitor<LocalLHSRewriter> {
public:
    LocalLHSRewriter(const clang::PrintingPolicy& policy,
                     clang::Rewriter& scratch)
        : printPolicy(policy), scratch(scratch) {}
    bool VisitVarDecl(clang::VarDecl* vd);
private:
    const clang::PrintingPolicy& printPolicy;
    clang::Rewriter& scratch;
};


class PmemPtrInterfaceRewriter : public clang::RecursiveASTVisitor<PmemPtrInterfaceRewriter> {
public:
    PmemPtrInterfaceRewriter(const clang::PrintingPolicy& policy,
                             clang::Rewriter& scratch)
        : printPolicy(policy), scratch(scratch) {}

    // Inserts .get() on field-read in raw-pointer rvalue contexts.
    bool VisitImplicitCastExpr(clang::ImplicitCastExpr* ice);

    // For locals initialized from a pmem_ptr field read, rewrites the LHS
    // to match (persistent<T>*).
    bool VisitVarDecl(clang::VarDecl* vd);

private:
    const clang::PrintingPolicy& printPolicy;
    clang::Rewriter& scratch;
};


// Rewrites the C-string idioms that operate on a field whose declared type is
// char* / const char* / std::string, since wrapFieldType remaps those fields to
// pmem_string:
//     field = new char[n];      -> statement removed (pmem_string owns storage)
//     strcpy(field, src);       -> field = src
//     delete[] field;           -> statement removed (destructor frees)
// All three must be rewritten: because pmem_string converts implicitly to
// const char*, the first and third would otherwise COMPILE and misbehave
// silently rather than failing loudly.
class CStringIdiomRewriter : public clang::RecursiveASTVisitor<CStringIdiomRewriter> {
public:
    CStringIdiomRewriter(clang::ASTContext& context, clang::Rewriter& scratch)
        : context(context), scratch(scratch) {}

    bool VisitBinaryOperator(clang::BinaryOperator* bo);
    bool VisitCallExpr(clang::CallExpr* ce);
    bool VisitCXXDeleteExpr(clang::CXXDeleteExpr* de);

private:
    clang::ASTContext& context;
    clang::Rewriter& scratch;

    // Non-null if `e` (after stripping parens/implicit casts) reads a field
    // whose declared type is string-like.
    const clang::FieldDecl* asStringField(const clang::Expr* e) const;
    std::string textOf(const clang::Expr* e) const;
    void removeStatement(const clang::Stmt* s);
};

#endif