#include "memberPersistifier.h"
#include "../../utils/utils.h"
#include "../../utils/typeTranslation.h"
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Lex/Lexer.h>
#include <llvm/Support/raw_ostream.h>
#include <functional>
#include <set>
#include <string>
#include <vector>

// ---- The rewriters ----
// Each runs over the original class's code (well-typed, unlike a copy pasted
// into persistent<C>) and edits a scratch copy of it; MemberPersistifier reads
// the result back as the text of the specialization's member.
namespace {
    void reportError(const clang::SourceManager& SM, clang::SourceLocation loc,
                     const std::string& what) {
        utils::reportError(SM.getFileLoc(loc).printToString(SM) + ": " + what);
    }

// `new T(args)` -> `new P(T)(args)` and `new T[n]` -> `new P(T)[n]`, so
// `new HashNode*[n]` becomes `new pmem_ptr<persistent<HashNode>>[n]`: every
// allocation in a method of a persistent class goes to pmem. Placement new
// is left alone, since it builds in memory someone else allocated.
class NewExprRewriter: public clang::RecursiveASTVisitor<NewExprRewriter> {
public:
    NewExprRewriter(const clang::PrintingPolicy& policy, QueueSpecializationFn queueSpecialization,
                    clang::Rewriter& scratch)
        : printPolicy(policy), queueSpecialization(std::move(queueSpecialization)), scratch(scratch) {}
    bool VisitCXXNewExpr(clang::CXXNewExpr* newExpr);
private:
    const clang::PrintingPolicy& printPolicy;
    QueueSpecializationFn queueSpecialization;
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
    LocalTypeRewriter(const clang::PrintingPolicy& policy, QueueSpecializationFn queueSpecialization,
                      clang::Rewriter& scratch)
        : printPolicy(policy), queueSpecialization(std::move(queueSpecialization)), scratch(scratch) {}
    bool VisitVarDecl(clang::VarDecl* vd);
private:
    const clang::PrintingPolicy& printPolicy;
    QueueSpecializationFn queueSpecialization;
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
    for (const std::string& c : tt.classes) queueSpecialization(c);
    scratch.ReplaceText(tsi->getTypeLoc().getUnqualifiedLoc().getSourceRange(), tt.prefix);
    return true;
}

    bool pointsIntoPmem(const clang::Expr* e, unsigned depth);

    // A local whose initialiser points into pmem was retyped to point there
    // too (LocalTypeRewriter), so reading it points into pmem.
    bool localPointsIntoPmem(const clang::DeclRefExpr* dre, unsigned depth) {
        const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl());
        if (!vd || llvm::isa<clang::ParmVarDecl>(vd) || !vd->isLocalVarDecl() || !vd->hasInit())
            return false;
        return depth < 8 && pointsIntoPmem(vd->getInit(), depth + 1);
    }

bool pointsIntoPmem(const clang::Expr* e) { return pointsIntoPmem(e, 0); }

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
    for (const std::string& c : tt.classes) queueSpecialization(c);

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
}   // namespace (the rewriters)

// ---- The read-only analysis ----
namespace {
    // True if `e` names a plain local variable, or a member or array element
    // of one: memory on the stack, which a write may touch without a
    // transaction. A pointer, a reference or `this` may lead into pmem.
    bool isDramLocal(const clang::Expr* e) {
        e = e->IgnoreParenImpCasts();
        for (;;) {
            if (const auto* me = llvm::dyn_cast<clang::MemberExpr>(e)) {
                if (me->isArrow()) return false;
                e = me->getBase()->IgnoreParenImpCasts();
            } else if (const auto* ase = llvm::dyn_cast<clang::ArraySubscriptExpr>(e)) {
                const clang::Expr* base = ase->getBase()->IgnoreParenImpCasts();
                if (!base->getType()->isArrayType()) return false;   // p[i] through a pointer
                e = base;
            } else {
                break;
            }
        }
        const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(e);
        const auto* v = dre ? llvm::dyn_cast<clang::VarDecl>(dre->getDecl()) : nullptr;
        return v && v->hasLocalStorage() && !v->getType()->isReferenceType();
    }

    // Looks for anything in a method body that may write persistent data:
    //   W1  a write (=, op=, ++, --) to anything but a plain local
    //   W2  new or delete
    //   W3  a call of a persistent class's method that is not read-only
    //   W4  a pointer to non-const or a non-const reference handed to any
    //       other function, which may write through it
    class WriteFinder : public clang::RecursiveASTVisitor<WriteFinder> {
    public:
        explicit WriteFinder(std::function<bool(const clang::CXXMethodDecl*)> methodIsReadOnly)
            : methodIsReadOnly(std::move(methodIsReadOnly)) {}
        bool writes = false;

        bool VisitBinaryOperator(clang::BinaryOperator* bo) {                    // W1
            if (bo->isAssignmentOp() && !isDramLocal(bo->getLHS())) writes = true;
            return !writes;
        }
        bool VisitUnaryOperator(clang::UnaryOperator* uo) {                      // W1
            if (uo->isIncrementDecrementOp() && !isDramLocal(uo->getSubExpr())) writes = true;
            return !writes;
        }
        bool VisitCXXNewExpr(clang::CXXNewExpr*)       { writes = true; return false; }   // W2
        bool VisitCXXDeleteExpr(clang::CXXDeleteExpr*) { writes = true; return false; }   // W2

        bool VisitCallExpr(clang::CallExpr* call) {
            const auto* method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(call->getCalleeDecl());
            const clang::Expr* object = nullptr;     // the object a method is called on
            if (const auto* mc = llvm::dyn_cast<clang::CXXMemberCallExpr>(call)) {
                object = mc->getImplicitObjectArgument();
                if (const auto* me = llvm::dyn_cast<clang::MemberExpr>(mc->getCallee()->IgnoreParens()))
                    if (me->isArrow()) object = nullptr;   // p->f(): an object through a pointer
            } else if (const auto* op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(call)) {
                if (method) object = op->getArg(0);        // a member operator: arg 0 is the object
                if (op->isAssignmentOp() || op->getOperator() == clang::OO_PlusPlus
                    || op->getOperator() == clang::OO_MinusMinus) {                  // W1
                    if (!isDramLocal(op->getArg(0))) writes = true;
                    return !writes;
                }
            }
            if (method && !method->isStatic() && !(object && isDramLocal(object))) {
                // A method of an object that may be in pmem.                       // W3
                if (!methodIsReadOnly(method)) writes = true;
                return !writes;
            }
            // Any other function: what it gets a writable handle to.               // W4
            const auto* fd = call->getDirectCallee();
            unsigned first = llvm::isa<clang::CXXOperatorCallExpr>(call) && method ? 1 : 0;
            for (unsigned i = first; i < call->getNumArgs() && !writes; i++) {
                const clang::Expr* arg = call->getArg(i);
                unsigned p = i - first;
                clang::QualType param = fd && p < fd->getNumParams()
                                      ? fd->getParamDecl(p)->getType() : arg->getType();
                bool writable = (param->isPointerType() && !param->getPointeeType().isConstQualified())
                             || (param->isReferenceType() && !param.getNonReferenceType().isConstQualified());
                if (writable && !isDramLocal(arg)) writes = true;
            }
            return !writes;
        }

    private:
        std::function<bool(const clang::CXXMethodDecl*)> methodIsReadOnly;
    };
}

// ---- MemberPersistifier ----

MemberPersistifier::MemberPersistifier(clang::ASTContext& context, const clang::PrintingPolicy& policy,
                                       QueueSpecializationFn queueSpecialization)
    : context(context), printPolicy(policy), queueSpecialization(std::move(queueSpecialization)) {}

std::string MemberPersistifier::persistifyDefinition(const clang::CXXMethodDecl* method) {
    if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(method)) {
        std::string def = persistifyConstructorDefinition(ctor);
        if (!def.empty()) return def;
    }
    clang::Rewriter scratch(context.getSourceManager(), context.getLangOpts());
    runRewriters(method->getBody(), scratch);

    clang::SourceRange bodyRange = method->getBody()->getSourceRange();
    std::string body = scratch.getRewrittenText(bodyRange);

    // A method that only reads needs no transaction (closure doc 8.2); if the
    // analysis were wrong, the library would still throw on its first write.
    if (isReadOnlyMethod(method))
        return " /* read-only: no transaction */ " + body;
    return " " + wrapInTransaction(body, method);
}

// A constructor with a member-initializer list keeps it as a real initializer
// list on the specialization, rather than turning it into body assignments.
// The list is copied as written, so C++ itself handles initialization order,
// name lookup (`size(size)` initializes the field from the parameter), and
// members without a default constructor; anything ill-formed in the original
// is ill-formed here too. Construction always runs inside an enclosing
// transaction (pmem_get_or_create, or the calling method), which is where the
// members' store() calls are logged.
// Returns "" for a constructor without a written initializer list, which
// persistifyDefinition() then treats like any other method.
std::string MemberPersistifier::persistifyConstructorDefinition(const clang::CXXConstructorDecl* ctor) {
    // The initializers live on the definition, which may be out of line.
    const clang::FunctionDecl* defFD = nullptr;
    if(!ctor->hasBody(defFD)) return "";
    const auto* def = llvm::cast<clang::CXXConstructorDecl>(defFD);

    std::vector<const clang::CXXCtorInitializer*> inits;
    for(const auto* init : def->inits())
        if(init->isWritten()) inits.push_back(init);
    if(inits.empty()) return "";

    clang::Rewriter scratch(context.getSourceManager(), context.getLangOpts());
    std::string initList;
    for(const auto* init : inits) {
        if(init->isBaseInitializer()) {
            utils::reportError("base-class initializer in constructor of "
                               + ctor->getParent()->getNameAsString()
                               + " is not supported (no inheritance in persistent<T>)");
            continue;
        }
        if(init->isIndirectMemberInitializer()) {
            utils::reportError("initializer of anonymous struct/union member in constructor of "
                               + ctor->getParent()->getNameAsString() + " is not supported");
            continue;
        }
        runRewriters(init->getInit(), scratch);
        // `Box(args)` delegates to another constructor, which is now named `persistent`.
        if(init->isDelegatingInitializer())
            scratch.ReplaceText(init->getTypeSourceInfo()->getTypeLoc().getSourceRange(), "persistent");
        if(!initList.empty()) initList += ", ";
        initList += scratch.getRewrittenText(init->getSourceRange());
    }

    runRewriters(def->getBody(), scratch);
    std::string body = scratch.getRewrittenText(def->getBody()->getSourceRange());

    std::string out = initList.empty() ? "" : " : " + initList;
    return out + " " + wrapInTransaction(body, ctor);
}

// A default member initializer (`int capacity = 16;`) is carried over to the
// wrapped field; without it persistent<T>'s default constructor zeroes the
// field. `= x` is kept where the wrapper converts implicitly (persistent<F>
// for fundamentals, pmem_ptr<..> for pointers to fundamentals): braces there
// would reject narrowing like `int x = 3.5`. A pointer to a class gets `{x}`,
// and persistent<Class> has explicit constructors, so it does too. A by-value class built in
// place (`Pt origin = Pt(4, 5);`) forwards its arguments, `origin{4, 5}`, since
// persistent<Pt> mirrors Pt's constructors but cannot be built from a raw Pt.
// Any other class-typed initializer (e.g. a call returning Pt) is left as
// `{expr}` and fails to compile at the spec.
std::string MemberPersistifier::persistifyDefaultMemberInit(const clang::FieldDecl* field) {
    clang::Expr* init = field->getInClassInitializer();
    if(!init) return "";

    if(llvm::isa<clang::StringLiteral>(init->IgnoreParenImpCasts())
       && field->getType()->isPointerType()) {
        utils::reportError(field->getLocation().printToString(context.getSourceManager())
                           + ": field '" + field->getNameAsString() + "': a string literal is in "
                             "DRAM, and a pointer in pmem cannot point at it; allocate with "
                             "new char[n] and copy (strcpy) in the constructor");
        return "";
    }

    clang::Rewriter scratch(context.getSourceManager(), context.getLangOpts());
    runRewriters(init, scratch);
    std::string text = scratch.getRewrittenText(init->getSourceRange());

    if(field->getInClassInitStyle() == clang::ICIS_ListInit) return text;

    clang::QualType qt = field->getType();
    if(qt->getAsCXXRecordDecl()) {
        const clang::Expr* e = init->IgnoreImplicit();
        if(const auto* cast = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(e))
            e = cast->getSubExpr()->IgnoreImplicit();
        const auto* ce = llvm::dyn_cast<clang::CXXConstructExpr>(e);
        if(ce && !ce->isListInitialization()
              && context.hasSameUnqualifiedType(ce->getType(), qt)) {
            std::string args;
            for(const clang::Expr* arg : ce->arguments()) {
                if(llvm::isa<clang::CXXDefaultArgExpr>(arg)) break;
                if(!args.empty()) args += ", ";
                args += scratch.getRewrittenText(arg->getSourceRange());
            }
            return "{" + args + "}";
        }
    }
    bool implicitWrapper = !qt->getAsCXXRecordDecl()
                        && !(qt->isPointerType() && qt->getPointeeType()->getAsCXXRecordDecl());
    return implicitWrapper ? " = " + text : "{" + text + "}";
}

void MemberPersistifier::runRewriters(clang::Stmt* s, clang::Rewriter& scratch) {
    NewExprRewriter v1(printPolicy, queueSpecialization, scratch);
    v1.TraverseStmt(s);

    LocalTypeRewriter v2(printPolicy, queueSpecialization, scratch);
    v2.TraverseStmt(s);

    CStringRewriter v3(context, scratch);
    v3.TraverseStmt(s);
}

bool MemberPersistifier::isReadOnlyMethod(const clang::CXXMethodDecl* method) {
    // Constructors and destructors build and tear down persistent objects.
    if (llvm::isa<clang::CXXConstructorDecl>(method) || llvm::isa<clang::CXXDestructorDecl>(method))
        return false;
    // A method of a library class may do anything; persistent classes are the user's.
    if (context.getSourceManager().isInSystemHeader(method->getLocation())) return method->isConst();
    const auto* key = method->getCanonicalDecl();
    if (auto known = readOnly.find(key); known != readOnly.end()) return known->second;
    // A body we cannot see may write. So may one we are already checking,
    // except for a direct recursive call: its writes are this body's own.
    const clang::FunctionDecl* def = nullptr;
    if (!method->hasBody(def) || checkingReadOnly.count(key)) return false;

    checkingReadOnly.insert(key);
    WriteFinder finder([&](const clang::CXXMethodDecl* callee) {
        return callee->getCanonicalDecl() == key || isReadOnlyMethod(callee);
    });
    finder.TraverseStmt(def->getBody());
    checkingReadOnly.erase(key);
    return readOnly[key] = !finder.writes;
}

std::string MemberPersistifier::wrapInTransaction(const std::string& bodyText,
                                                  const clang::CXXMethodDecl* method) {
    std::string body = bodyText;

    // Wrap the body in a PMDK transaction. Nested transactions are flattened
    // by PMDK, so wrapping every method body is safe even when the caller is
    // already inside a transaction.
    //
    // pmem::obj::transaction::run itself returns void, so for non-void
    // methods we wrap the body in an inner IIFE that captures the lambda's
    // return value into a local. Exceptions still propagate through
    // transaction::run (it aborts the txn and rethrows), so the post-tx
    // read of __pers_ret is only reached on success.
    //
    // Known limits on the return type T (compile-error at the user's spec
    // if violated): T must be default-constructible, assignable, and not a
    // reference type.
    bool isCtorOrDtor = llvm::isa<clang::CXXConstructorDecl>(method)
                     || llvm::isa<clang::CXXDestructorDecl>(method);
    bool isVoidReturn = isCtorOrDtor || method->getReturnType()->isVoidType();

    if (isVoidReturn) {
        body = "{\n        pmem::obj::transaction::run(pmem_pool(), [&]() "
             + body + ");\n    }";
    } else {
        std::string retType = utils::signatureType(method->getReturnType(), printPolicy);
        body = "{\n        " + retType + " __pers_ret = {};\n"
             + "        pmem::obj::transaction::run(pmem_pool(), [&]() {\n"
             + "            __pers_ret = [&]() -> " + retType + " " + body + "();\n"
             + "        });\n"
             + "        return __pers_ret;\n    }";
    }

    return body;
}
