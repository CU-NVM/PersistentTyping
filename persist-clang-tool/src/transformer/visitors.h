#ifndef VISITORS_H
#define VISITORS_H


#include "recursePersistentTyper.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/Support/raw_ostream.h>
#include "../utils/utils.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Lex/Lexer.h>

// `new T(args)` -> `new P(T)(args)` and `new T[n]` -> `new P(T)[n]`, so
// `new HashNode*[n]` becomes `new pmem_ptr<persistent<HashNode>>[n]`: every
// allocation in a method of a persistent class goes to pmem. Placement new
// is left alone, since it builds in memory someone else allocated.
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

// Retypes a local so it can hold what the rewritten code gives it. Locals
// stay raw pointers in DRAM, but what they point at is now persistent:
//   HashNode* nd        -> persistent<HashNode>* nd                 (L(U*) = P(U)*)
//   HashNode** pp       -> pmem_ptr<persistent<HashNode>>* pp
//   Node& r             -> persistent<Node>& r
//   int* p = data;      -> persistent<int>* p = data;   only when initialised
//                          from pmem (a field, a `new`), see pointsIntoPmem
// Only the pointee is replaced, so `Node *a, *b` and `const Node*` keep the
// rest of their declarator as written; an `auto` type is spelled out.
class LocalTypeRewriter : public clang::RecursiveASTVisitor<LocalTypeRewriter> {
public:
    LocalTypeRewriter(const clang::PrintingPolicy& policy, RecursePersistentTyper* typer,
                      clang::Rewriter& scratch)
        : printPolicy(policy), typer(typer), scratch(scratch) {}
    bool VisitVarDecl(clang::VarDecl* vd);
private:
    const clang::PrintingPolicy& printPolicy;
    RecursePersistentTyper* typer;
    clang::Rewriter& scratch;
    std::set<unsigned> replaced;    // begin offsets already rewritten (shared base types)

    void replaceOnce(clang::SourceRange range, const std::string& text);
};

// True if `e` yields an address in pmem: a `new` (which NewExprRewriter sends
// to pmem), a field, anything reached from one (`p->f`, `a[i]`, `&a[i]`,
// `*p`, `a + i`), or a local initialised from one.
bool pointsIntoPmem(const clang::Expr* e);

// A C string in pmem is an array of persistent<char>, but the C library takes
// char*. Each such argument is wrapped in a library helper:
//   read   strlen(name)       -> strlen(persist_detail::chars(name))
//   write  strcpy(name, s)    -> strcpy(persist_detail::writable(name, std::strlen(s) + 1), s)
// The bytes a write touches come from the function: strcpy, strncpy, strcat,
// strncat, memcpy, memmove, memset. Another function taking a non-const
// pointer into a pmem string is an error, since its writes could not be
// logged. Pointing a pmem string at a string literal (DRAM) is an error too.
class CStringRewriter : public clang::RecursiveASTVisitor<CStringRewriter> {
public:
    CStringRewriter(clang::ASTContext& context, clang::Rewriter& scratch)
        : context(context), scratch(scratch) {}
    bool VisitCallExpr(clang::CallExpr* ce);
    bool VisitCXXConstructExpr(clang::CXXConstructExpr* ce);
    bool VisitBinaryOperator(clang::BinaryOperator* bo);
private:
    clang::ASTContext& context;
    clang::Rewriter& scratch;

    void rewriteArgs(const clang::FunctionDecl* fd, llvm::ArrayRef<const clang::Expr*> args,
                     unsigned firstParam);
    std::string text(const clang::Expr* e) const;
    std::string readText(const clang::Expr* e) const;
    void wrap(const clang::Expr* e, const std::string& before, const std::string& after);
};

// A pointer-to-char expression that points into pmem: a C string in pmem.
bool isPmemCString(const clang::Expr* e);


#endif