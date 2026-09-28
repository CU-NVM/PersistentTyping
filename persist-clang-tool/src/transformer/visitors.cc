#include "recursePersistentTyper.h"
#include "visitors.h"
#include "../utils/utils.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/Support/raw_ostream.h>
#include "../utils/utils.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Lex/Lexer.h>

bool NewExprRewriter::VisitCXXNewExpr(clang::CXXNewExpr* newExpr) {
    clang::QualType allocType = newExpr->getAllocatedType();
    if(!allocType->getAsCXXRecordDecl()) return true;
    std::string typeName = allocType.getAsString(printPolicy);

    typer->specializeType(typeName);

    if (auto* tsi = newExpr->getAllocatedTypeSourceInfo()) {
        clang::SourceRange typeRange = tsi->getTypeLoc().getSourceRange();
        std::string newText = "persistent<" + typeName + ">";
        scratch.ReplaceText(typeRange, newText);
    }
    return true;
}

// KNOWN LIMITATION (fundamental flaw — needs Approach 3 / function-variant generation):
// If a local is initialized with `new T()` and later reassigned from a non-persistent
// source, e.g.
//      Node* n = new Node();    // we rewrite both sides → persistent<Node>*
//      n = some_dram_func();    // FAILS to compile (Node* not convertible to persistent<Node>*)
// the post-rewrite code won't compile. Phase 5 doesn't (yet) emit
// `persistent<T>*` variants of free functions, so the user must refactor the
// original code to use separate variables. See HANDOFF.md.
bool LocalLHSRewriter::VisitVarDecl(clang::VarDecl* vd) {
    if(llvm::isa<clang::ParmVarDecl>(vd)) return true; // Don't rewrite parameters (handled by a different function)
    if(!vd->hasInit()) return true; // Only rewrite initialized variables for now (heuristic for "local")


    const clang::Expr* init = vd->getInit()->IgnoreParenImpCasts();
    const auto* newExpr = llvm::dyn_cast<clang::CXXNewExpr>(init);
    if(!newExpr) return true; // Only rewrite variables directly initialized by new.
    
    clang::QualType allocType = newExpr->getAllocatedType();
    if (!allocType->getAsCXXRecordDecl()) return true; // Only rewrite if allocating a class/struct type.
    std::string typeName = allocType.getAsString(printPolicy);

    auto* tsi = vd->getTypeSourceInfo();
    if (!tsi) return true; // No source info, can't rewrite
    auto ptl = tsi->getTypeLoc().getAs<clang::PointerTypeLoc>();
    if (ptl.isNull()) return true; // Not a pointer type loc, can't rewrite
    clang::SourceRange pointeeRange = ptl.getPointeeLoc().getSourceRange();
    //(the * is preserved because it was outside pointeeRange)
    std::string newText = "persistent<" + typeName + ">";
    scratch.ReplaceText(pointeeRange, newText);
    return true;
}

bool PmemPtrInterfaceRewriter::VisitImplicitCastExpr(clang::ImplicitCastExpr* ice) {
    if(ice->getCastKind() != clang::CK_LValueToRValue) return true;
    const auto* me = llvm::dyn_cast<clang::MemberExpr>(ice->getSubExpr());
    if (!me) return true;

    const auto* fd = llvm::dyn_cast<clang::FieldDecl>(me->getMemberDecl());
    if(!fd) return true;
    if(!utils::isUserClassPointerType(fd->getType())) return true;

    clang::SourceLocation insertLoc = clang::Lexer::getLocForEndOfToken(
        me->getSourceRange().getEnd(), 0,
        scratch.getSourceMgr(), scratch.getLangOpts());
    scratch.InsertText(insertLoc, ".get()");
    return true;
}

bool PmemPtrInterfaceRewriter::VisitVarDecl(clang::VarDecl* vd) {
    if (llvm::isa<clang::ParmVarDecl>(vd)) return true;
    if (!vd->hasInit()) return true;

    // Only rewrite when the initializer is a MemberExpr reading a pmem_ptr field.
    const clang::Expr* init = vd->getInit()->IgnoreParenImpCasts();
    const auto* me = llvm::dyn_cast<clang::MemberExpr>(init);
    if (!me) return true;
    const auto* fd = llvm::dyn_cast<clang::FieldDecl>(me->getMemberDecl());
    if (!fd) return true;
    if (!utils::isUserClassPointerType(fd->getType())) return true;

    clang::QualType pointee = fd->getType()->getPointeeType();
    std::string typeName = pointee.getAsString(printPolicy);

    // Rewrite the local's pointee type: `T* var` → `persistent<T>* var`.
    auto* tsi = vd->getTypeSourceInfo();
    if (!tsi) return true;
    auto ptl = tsi->getTypeLoc().getAs<clang::PointerTypeLoc>();
    if (ptl.isNull()) return true;
    clang::SourceRange pointeeRange = ptl.getPointeeLoc().getSourceRange();
    scratch.ReplaceText(pointeeRange, "persistent<" + typeName + ">");
    return true;
}

const clang::FieldDecl*
CStringIdiomRewriter::asStringField(const clang::Expr* e) const {
    if (!e) return nullptr;
    const auto* me = llvm::dyn_cast<clang::MemberExpr>(e->IgnoreParenImpCasts());
    if (!me) return nullptr;
    const auto* fd = llvm::dyn_cast<clang::FieldDecl>(me->getMemberDecl());
    if (!fd) return nullptr;
    return utils::isStringLikeType(fd->getType()) ? fd : nullptr;
}

std::string CStringIdiomRewriter::textOf(const clang::Expr* e) const {
    return clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(e->getSourceRange()),
        context.getSourceManager(), context.getLangOpts()).str();
}

void CStringIdiomRewriter::removeStatement(const clang::Stmt* s) {
    clang::SourceLocation end = s->getEndLoc();
    if (auto next = clang::Lexer::findNextToken(
            end, context.getSourceManager(), context.getLangOpts())) {
        if (next->is(clang::tok::semi)) end = next->getEndLoc();
    }
    clang::Rewriter::RewriteOptions opts;
    opts.RemoveLineIfEmpty = true;
    scratch.RemoveText(
        clang::CharSourceRange::getTokenRange(s->getBeginLoc(), end), opts);
}

// field = new char[n];  ->  removed. pmem_string allocates its own storage in
// assign(); keeping this would copy from an uninitialised DRAM buffer and leak it.
bool CStringIdiomRewriter::VisitBinaryOperator(clang::BinaryOperator* bo) {
    if (bo->getOpcode() != clang::BO_Assign) return true;
    if (!asStringField(bo->getLHS())) return true;
    const auto* ne = llvm::dyn_cast<clang::CXXNewExpr>(bo->getRHS()->IgnoreParenImpCasts());
    if (!ne || !ne->isArray()) return true;
    removeStatement(bo);
    return true;
}

// strcpy(field, src);  ->  field = src;
// strncpy is deliberately not handled: dropping the length argument would
// change semantics (truncation, null-padding), so it is left to fail loudly.
bool CStringIdiomRewriter::VisitCallExpr(clang::CallExpr* ce) {
    const auto* callee = ce->getDirectCallee();
    if (!callee || callee->getNameAsString() != "strcpy") return true;
    if (ce->getNumArgs() < 2) return true;
    if (!asStringField(ce->getArg(0))) return true;
    scratch.ReplaceText(
        clang::CharSourceRange::getTokenRange(ce->getSourceRange()),
        textOf(ce->getArg(0)) + " = " + textOf(ce->getArg(1)));
    return true;
}

// delete[] field;  ->  removed. Without this the implicit conversion to
// const char* hands the pmem buffer to the DRAM deallocator.
bool CStringIdiomRewriter::VisitCXXDeleteExpr(clang::CXXDeleteExpr* de) {
    if (!de->isArrayForm()) return true;
    if (!asStringField(de->getArgument())) return true;
    removeStatement(de);
    return true;
}
