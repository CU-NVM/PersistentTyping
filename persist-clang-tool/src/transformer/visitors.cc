#include "recursePersistentTyper.h"
#include "visitors.h"
#include "../utils/utils.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/Support/raw_ostream.h>
#include "../utils/utils.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Lex/Lexer.h>

namespace {
    void reportError(const clang::SourceManager& SM, clang::SourceLocation loc,
                     const std::string& what) {
        utils::reportError(SM.getFileLoc(loc).printToString(SM) + ": " + what);
    }
}

bool NewExprRewriter::VisitCXXNewExpr(clang::CXXNewExpr* newExpr) {
    if (newExpr->getNumPlacementArgs() > 0) return true;
    auto* tsi = newExpr->getAllocatedTypeSourceInfo();
    if (!tsi) return true;

    // For `new T[n]` the allocated type is T, the element type, and the
    // TypeLoc covers just `T`; `[n]` is the separate array-size expression.
    // A `const` is kept as written, so translate the unqualified type.
    clang::QualType allocType = newExpr->getAllocatedType();
    utils::TypeTranslation tt = utils::translateType(allocType.getUnqualifiedType(), printPolicy);
    const clang::SourceManager& SM = scratch.getSourceMgr();
    if (!tt.ok()) {
        reportError(SM, newExpr->getBeginLoc(), "new " + allocType.getAsString(printPolicy) + ": " + tt.error);
        return true;
    }
    if (!tt.suffix.empty()) {
        reportError(SM, newExpr->getBeginLoc(),
                    "new of a multi-dimensional array is not supported yet");
        return true;
    }
    for (const std::string& c : tt.classes) typer->specializeType(c);
    scratch.ReplaceText(tsi->getTypeLoc().getUnqualifiedLoc().getSourceRange(), tt.prefix);
    return true;
}

namespace {
    bool pointsIntoPmem(const clang::Expr* e, unsigned depth);

    // A local whose initialiser points into pmem was retyped to point there
    // too (LocalTypeRewriter), so reading it points into pmem.
    bool localPointsIntoPmem(const clang::DeclRefExpr* dre, unsigned depth) {
        const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl());
        if (!vd || llvm::isa<clang::ParmVarDecl>(vd) || !vd->isLocalVarDecl() || !vd->hasInit())
            return false;
        return depth < 8 && pointsIntoPmem(vd->getInit(), depth + 1);
    }
}

bool pointsIntoPmem(const clang::Expr* e) { return pointsIntoPmem(e, 0); }

namespace {
bool pointsIntoPmem(const clang::Expr* e, unsigned depth) {
    if (!e) return false;
    e = e->IgnoreParenImpCasts();
    if (const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(e))
        return localPointsIntoPmem(dre, depth);
    if (llvm::isa<clang::CXXNewExpr>(e)) return true;
    if (const auto* me = llvm::dyn_cast<clang::MemberExpr>(e))
        return llvm::isa<clang::FieldDecl>(me->getMemberDecl());
    if (const auto* ase = llvm::dyn_cast<clang::ArraySubscriptExpr>(e))
        return pointsIntoPmem(ase->getBase(), depth);
    if (const auto* uo = llvm::dyn_cast<clang::UnaryOperator>(e))
        if (uo->getOpcode() == clang::UO_AddrOf || uo->getOpcode() == clang::UO_Deref)
            return pointsIntoPmem(uo->getSubExpr(), depth);
    if (const auto* bo = llvm::dyn_cast<clang::BinaryOperator>(e))
        if (bo->isAdditiveOp())
            return pointsIntoPmem(bo->getLHS(), depth) || pointsIntoPmem(bo->getRHS(), depth);
    return false;
}
}

void LocalTypeRewriter::replaceOnce(clang::SourceRange range, const std::string& text) {
    unsigned begin = scratch.getSourceMgr().getFileOffset(range.getBegin());
    if (replaced.insert(begin).second)
        scratch.ReplaceText(range, text);
}

// KNOWN LIMITATION: a local that is retyped here and later assigned from DRAM,
// e.g. `Node* n = new Node(); n = some_dram_func();`, no longer compiles
// (Node* is not convertible to persistent<Node>*). See HANDOFF.md.
bool LocalTypeRewriter::VisitVarDecl(clang::VarDecl* vd) {
    if (llvm::isa<clang::ParmVarDecl>(vd)) return true;    // signatures: buildMethodSignature
    auto* tsi = vd->getTypeSourceInfo();
    if (!tsi) return true;
    clang::QualType t = vd->getType();
    if (!t->isPointerType() && !t->isReferenceType()) return true;

    utils::TypeTranslation tt = utils::translateDramRef(t, printPolicy);
    if (!tt.ok()) return true;      // void*, function pointers, library types: not ours
    if (tt.classes.empty() && !(vd->hasInit() && pointsIntoPmem(vd->getInit())))
        return true;                // e.g. `int* p = &local;` stays a DRAM pointer
    for (const std::string& c : tt.classes) typer->specializeType(c);

    clang::TypeLoc tl = tsi->getTypeLoc();
    clang::TypeLoc pointeeLoc;
    if (auto ptl = tl.getUnqualifiedLoc().getAs<clang::PointerTypeLoc>())
        pointeeLoc = ptl.getPointeeLoc();
    else if (auto rtl = tl.getUnqualifiedLoc().getAs<clang::ReferenceTypeLoc>())
        pointeeLoc = rtl.getPointeeLoc();

    // `auto`, a typedef of a pointer, ...: spell the whole type out.
    if (pointeeLoc.isNull() || tl.getContainedAutoTypeLoc()) {
        replaceOnce(tl.getSourceRange(), tt.prefix);
        return true;
    }
    clang::QualType pointee = t->getPointeeType();
    utils::TypeTranslation inner = utils::translateType(pointee.getUnqualifiedType(), printPolicy);
    replaceOnce(pointeeLoc.getUnqualifiedLoc().getSourceRange(), inner.prefix);
    return true;
}

bool isPmemCString(const clang::Expr* e) {
    if (!e) return false;
    const clang::Expr* inner = e->IgnoreParenImpCasts();
    clang::QualType t = inner->getType();
    clang::QualType elem;
    if (t->isPointerType()) elem = t->getPointeeType();
    else if (t->isArrayType()) elem = t->getAsArrayTypeUnsafe()->getElementType();
    else return false;
    return elem->isCharType() && pointsIntoPmem(inner);
}

std::string CStringRewriter::text(const clang::Expr* e) const {
    const clang::SourceManager& SM = context.getSourceManager();
    return clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(SM.getFileLoc(e->getBeginLoc()),
                                              SM.getFileLoc(e->getEndLoc())),
        SM, context.getLangOpts()).str();
}

// An argument as the length of a write may read it: through chars() if it
// is a pmem string itself.
std::string CStringRewriter::readText(const clang::Expr* e) const {
    return isPmemCString(e) ? "persist_detail::chars(" + text(e) + ")" : text(e);
}

void CStringRewriter::wrap(const clang::Expr* e, const std::string& before,
                           const std::string& after) {
    const clang::SourceManager& SM = context.getSourceManager();
    if (e->getBeginLoc().isMacroID() || e->getEndLoc().isMacroID()) {
        reportError(SM, e->getBeginLoc(),
                    "a C string in pmem is passed inside a macro, which cannot be rewritten");
        return;
    }
    scratch.InsertTextBefore(e->getBeginLoc(), before);
    scratch.InsertTextAfterToken(e->getEndLoc(), after);
}

void CStringRewriter::rewriteArgs(const clang::FunctionDecl* fd,
                                  llvm::ArrayRef<const clang::Expr*> args, unsigned firstParam) {
    std::string name = fd && fd->getDeclName().isIdentifier() ? fd->getName().str() : "";
    for (unsigned i = 0; i < args.size(); i++) {
        const clang::Expr* arg = args[i];
        if (!isPmemCString(arg)) continue;
        unsigned p = i - firstParam;
        bool variadic = !fd || i < firstParam || p >= fd->getNumParams();
        clang::QualType param = variadic ? clang::QualType() : fd->getParamDecl(p)->getType();
        bool writes = !variadic && param->isPointerType()
                      && !param->getPointeeType().isConstQualified();
        if (!writes) {
            wrap(arg, "persist_detail::chars(", ")");
            continue;
        }
        // The bytes the call writes at its destination, argument 0.
        std::string n;
        auto a = [&](unsigned k) { return k < args.size() ? args[k] : nullptr; };
        if (p == 0 && (name == "strcpy") && a(1))
            n = "std::strlen(" + readText(a(1)) + ") + 1";
        else if (p == 0 && name == "strcat" && a(1))
            n = "std::strlen(" + readText(arg) + ") + std::strlen(" + readText(a(1)) + ") + 1";
        else if (p == 0 && name == "strncat" && a(2))
            n = "std::strlen(" + readText(arg) + ") + (" + text(a(2)) + ") + 1";
        else if (p == 0 && (name == "strncpy" || name == "memcpy" || name == "memmove"
                            || name == "memset") && a(2))
            n = "(" + text(a(2)) + ")";
        if (n.empty()) {
            reportError(context.getSourceManager(), arg->getBeginLoc(),
                        "'" + (name.empty() ? std::string("this call") : name)
                        + "' may write to a C string in pmem through char*, which cannot be "
                          "logged; use strcpy, strncpy, strcat, strncat, memcpy, memmove or "
                          "memset, or write element by element");
            continue;
        }
        wrap(arg, "persist_detail::writable(", ", " + n + ")");
    }
}

bool CStringRewriter::VisitCallExpr(clang::CallExpr* ce) {
    const clang::FunctionDecl* fd = ce->getDirectCallee();
    // A member operator call lists the object as argument 0.
    unsigned first = llvm::isa<clang::CXXOperatorCallExpr>(ce) && fd
                     && llvm::isa<clang::CXXMethodDecl>(fd)
                     && !llvm::cast<clang::CXXMethodDecl>(fd)->isStatic() ? 1 : 0;
    std::vector<const clang::Expr*> args(ce->arg_begin(), ce->arg_end());
    rewriteArgs(fd, args, first);
    return true;
}

bool CStringRewriter::VisitCXXConstructExpr(clang::CXXConstructExpr* ce) {
    std::vector<const clang::Expr*> args(ce->arg_begin(), ce->arg_end());
    rewriteArgs(ce->getConstructor(), args, 0);
    return true;
}

// `name = "abc"` would make a pmem pointer point at a string literal, which
// lives in DRAM; the program has to allocate and copy instead.
bool CStringRewriter::VisitBinaryOperator(clang::BinaryOperator* bo) {
    if (bo->getOpcode() != clang::BO_Assign) return true;
    if (!llvm::isa<clang::StringLiteral>(bo->getRHS()->IgnoreParenImpCasts())) return true;
    if (!bo->getLHS()->getType()->isPointerType() || !pointsIntoPmem(bo->getLHS())) return true;
    reportError(context.getSourceManager(), bo->getBeginLoc(),
                "a string literal is in DRAM, and a pointer in pmem cannot point at it; "
                "allocate with new char[n] and copy (strcpy)");
    return true;
}
