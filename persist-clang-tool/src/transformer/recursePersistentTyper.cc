#include "recursePersistentTyper.h"
#include "visitors.h"
#include <llvm/Support/raw_ostream.h>
#include "../utils/utils.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Lex/Lexer.h>

RecursePersistentTyper::RecursePersistentTyper(clang::ASTContext &context,
                                               clang::Rewriter &rewriter)
    : context(context),
      rewriter(rewriter),
      printPolicy(context.getPrintingPolicy()) {
    printPolicy.SuppressTagKeyword = true;
}

void RecursePersistentTyper::start() {
    llvm::outs() << "Starting RecursePersistentTyper\n";
    unsigned errorsBefore = utils::errorCount();

    existingSpecs = utils::findExistingPersistentSpecs(context, printPolicy);

    discoverTypesFromRoot();

    while(!workList.empty()) {
        std::string name = workList.back();
        workList.pop_back();
        const auto* rd = utils::findCXXRecordDeclByName(context, name, printPolicy);
        if(!rd){
            // Defined in another file of the program: its specialisation is
            // generated in the TU that defines it. If no file does, the build
            // stops at the library's "never specialised" check.
            llvm::outs() << "note: " << name << " is not defined in this translation unit; "
                            "persistent<" << name << "> is generated where it is defined\n";
            continue;
        }
        std::string text = processClass(name , rd);
        specs[name] = {rd, text};
    }
    auto& SM = context.getSourceManager();

    // Insert in the order the classes end: two specs can land at the same
    // place (after one namespace or enclosing class), and a spec must follow
    // the specs of the classes it uses. A class can only use one that ends
    // before it, a by-value field's class or a nested class, so ordering by
    // end puts persistent<Outer::Inner> before persistent<Outer>.
    std::vector<const Spec*> ordered;
    for (const auto& [name, spec] : specs) ordered.push_back(&spec);
    std::stable_sort(ordered.begin(), ordered.end(), [&](const Spec* a, const Spec* b) {
        return SM.isBeforeInTranslationUnit(a->rd->getEndLoc(), b->rd->getEndLoc());
    });

    std::set<unsigned> includeChecked;   // FileID hash values, dedupes per file
    std::set<unsigned> marked;           // files given TRANSFORMED_DEFINE
    for (const Spec* spec : ordered) {
        clang::SourceLocation insertLoc = specInsertLoc(spec->rd);
        rewriter.InsertTextAfterToken(insertLoc, "\n" + spec->text);

        // Ensure the file owning this spec pulls in the persistent library.
        clang::FileID fid = SM.getFileID(insertLoc);
        if (includeChecked.insert(fid.getHashValue()).second) {
            if (!utils::fileIncludes(SM, fid, "persistenttype.hpp")) {
                rewriter.InsertText(SM.getLocForStartOfFile(fid),
                                    "#include \"persistenttype.hpp\"\n\n");
            }
            markTransformed(fid, marked);
        }
    }
    // This is the last pass, so the TU's own file is now fully transformed.
    markTransformed(SM.getMainFileID(), marked);

    // Like the other passes, write nothing rather than half a transformation.
    if (utils::errorCount() != errorsBefore) {
        llvm::errs() << "recurse-persistent: not writing "
                     << SM.getFileEntryRefForID(SM.getMainFileID())->getName() << " because of the errors above\n";
        return;
    }
    utils::writeAllRewrites(rewriter, context);
}  

// C++ only allows `template<> class persistent<C>` at namespace scope of the
// template, here the global one. So a spec goes right after the outermost
// namespace or class enclosing C: after `namespace app { ... }` for
// app::Node, after `struct Outer { ... };` for Outer::Inner.
clang::SourceLocation RecursePersistentTyper::specInsertLoc(const clang::CXXRecordDecl* rd) {
    const clang::Decl* top = rd;
    for (const clang::DeclContext* dc = rd->getDeclContext(); !dc->isTranslationUnit();
         dc = dc->getParent()) {
        if (dc->isTransparentContext()) continue;   // extern "C++" { ... }
        top = llvm::cast<clang::Decl>(dc);
    }
    auto& SM = context.getSourceManager();
    clang::SourceLocation end = SM.getFileLoc(top->getSourceRange().getEnd());
    if (llvm::isa<clang::RecordDecl>(top))
        if (auto next = clang::Lexer::findNextToken(end, SM, context.getLangOpts()))
            if (next->is(clang::tok::semi)) return next->getLocation();
    return end;
}

// Puts utils::TRANSFORMED_DEFINE first in the file, once.
void RecursePersistentTyper::markTransformed(clang::FileID fid, std::set<unsigned>& marked) {
    if (!marked.insert(fid.getHashValue()).second) return;
    auto& SM = context.getSourceManager();
    if (utils::hasTransformedDefine(SM.getBufferData(fid))) return;
    rewriter.InsertText(SM.getLocForStartOfFile(fid), utils::TRANSFORMED_DEFINE,
                        /*InsertAfter=*/false);
}

void RecursePersistentTyper::specializeType(const std::string& typeName) {
    if(existingSpecs.count(typeName)) return;
    if(!seen.insert(typeName).second) return;
    workList.push_back(typeName);
}

void RecursePersistentTyper::discoverTypesFromRoot() {
    auto* tu = context.getTranslationUnitDecl();
    const clang::RecordDecl* root = nullptr;
    for(const auto* decl : tu->decls()) {
        if(const auto* rd = llvm::dyn_cast<clang::RecordDecl>(decl)) {
            if(rd->getNameAsString() == "__pers_root") {
                root = rd;
                break;
            }
        }
    }
    if(!root) {
        utils::reportError("no __pers_root struct found in translation unit; run root-setup first");
        return;
    }
    for(const auto* field : root->fields()) {
        utils::TypeTranslation tt = utils::translateType(field->getType(), printPolicy);
        if (!tt.ok()) {
            utils::reportError("root '" + field->getNameAsString() + "': " + tt.error);
            continue;
        }
        for (const std::string& name : tt.classes) specializeType(name);
    }
}

const std::map<std::string, RecursePersistentTyper::Spec>&
RecursePersistentTyper::getSpecs() const {
    return specs;
}

std::string RecursePersistentTyper::processClass(const std::string& name,
                                                 const clang::CXXRecordDecl* rd) {
    struct Bucket {
        std::string fields;
        std::string methods;
    };
    Bucket pub,prot,priv;
    auto bucketFor = [&](clang::AccessSpecifier as) -> Bucket& {
        switch(as) {
            case clang::AS_public: return pub;
            case clang::AS_protected: return prot;
            default: return priv;
        }
    };

    for(const auto* field : rd->fields()) {
        Bucket& b = bucketFor(field->getAccess());
        std::vector<std::string> reached;
        b.fields += "    " + utils::wrapFieldType(field, printPolicy, reached)
                  + processInClassInit(field) + ";\n";
        for (const std::string& c : reached) specializeType(c);
    }

     // Operator new/delete — always public.
    pub.methods += "    static void* operator new(std::size_t sz)   { return pmem_alloc(sz, alignof(" + name + ")); }\n";
    pub.methods += "    static void* operator new[](std::size_t sz) { return pmem_alloc(sz, alignof(" + name + ")); }\n";
    pub.methods += "    static void  operator delete(void* p)       { pmem_free(p); }\n";
    pub.methods += "    static void  operator delete[](void* p)     { pmem_free(p); }\n";

                            
    for(const auto* method : rd->methods()) {
        if(method->isImplicit()) continue;
        if(method->isOverloadedOperator()) {
            // Dropping it would leave persistent<C> without the operator,
            // and code using it would fail far from the cause.
            utils::reportError(method->getLocation().printToString(context.getSourceManager())
                               + ": " + name + "::" + method->getNameAsString()
                               + ": overloaded operators are not supported in persistent classes yet");
            continue;
        }
        Bucket& b = bucketFor(method->getAccess());
        if(const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(method))
            b.methods += "    " + processConstructor(ctor) + "\n";
        else
            b.methods += "    " + processMethod(method) + "\n";
    }

    std::string out = "\ntemplate<>\nclass persistent<" + name + "> {\n";
    auto emit = [&](const char* label, const Bucket& b) {
        if(b.fields.empty() && b.methods.empty()) return;
        out += std::string(label) + ":\n";
        out += b.fields;
        out += b.methods;
    };
    emit("public", pub);
    emit("protected", prot);
    emit("private", priv);
    out += "};\n";
    return out;
}

// A constructor with a member-initializer list keeps it as a real initializer
// list on the specialization, rather than turning it into body assignments.
// The list is copied as written, so C++ itself handles initialization order,
// name lookup (`size(size)` initializes the field from the parameter), and
// members without a default constructor; anything ill-formed in the original
// is ill-formed here too. Construction always runs inside an enclosing
// transaction (pmem_get_or_create, or the calling method), which is where the
// members' store() calls are logged.
std::string RecursePersistentTyper::processConstructor(const clang::CXXConstructorDecl* ctor) {
    // The initializers live on the definition, which may be out of line.
    const clang::FunctionDecl* defFD = nullptr;
    if(!ctor->hasBody(defFD)) return processMethod(ctor);
    const auto* def = llvm::cast<clang::CXXConstructorDecl>(defFD);

    std::vector<const clang::CXXCtorInitializer*> inits;
    for(const auto* init : def->inits())
        if(init->isWritten()) inits.push_back(init);
    if(inits.empty()) return processMethod(ctor);

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

    specializeSignatureTypes(ctor);
    std::string sig = utils::buildMethodSignature(ctor, printPolicy);
    if(!initList.empty()) sig += " : " + initList;
    return sig + " " + wrapInTransaction(body, ctor) + "\n";
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
std::string RecursePersistentTyper::processInClassInit(const clang::FieldDecl* field) {
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

void RecursePersistentTyper::runRewriters(clang::Stmt* s, clang::Rewriter& scratch) {
    NewExprRewriter v1(printPolicy, this, scratch);
    v1.TraverseStmt(s);

    LocalTypeRewriter v2(printPolicy, this, scratch);
    v2.TraverseStmt(s);

    CStringRewriter v3(context, scratch);
    v3.TraverseStmt(s);
}

// A class a method takes or returns by pointer or reference is spelled
// persistent<C> in the signature, so it needs a specialisation too.
void RecursePersistentTyper::specializeSignatureTypes(const clang::CXXMethodDecl* method) {
    std::vector<clang::QualType> types = {method->getReturnType()};
    for (const auto* param : method->parameters()) types.push_back(param->getType());
    for (clang::QualType t : types) {
        utils::TypeTranslation tt = utils::translateDramRef(t, printPolicy);
        if (!tt.ok()) continue;
        for (const std::string& c : tt.classes) specializeType(c);
    }
}

std::string RecursePersistentTyper::processMethod(const clang::CXXMethodDecl* method) {
    specializeSignatureTypes(method);
    std::string sig = utils::buildMethodSignature(method, printPolicy);

    if(method->isDefaulted() && !method->doesThisDeclarationHaveABody())
        return sig + " = default;";
    if(method->isDeletedAsWritten())
        return sig + " = delete;";
    if(!method->hasBody())
        return sig + ";";

    clang::Rewriter scratch(context.getSourceManager(), context.getLangOpts());
    runRewriters(method->getBody(), scratch);

    clang::SourceRange bodyRange = method->getBody()->getSourceRange();
    std::string body = scratch.getRewrittenText(bodyRange);

    return sig + " " + wrapInTransaction(body, method) + "\n";
}

std::string RecursePersistentTyper::wrapInTransaction(const std::string& bodyText,
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
