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

    existingSpecs = utils::findExistingPersistentSpecs(context, printPolicy);

    discoverTypesFromRoot();

    while(!workList.empty()) {
        std::string name = workList.back();
        workList.pop_back();
        const auto* rd = utils::findCXXRecordDeclByName(context, name);
        if(!rd){
            llvm::outs() << " skipping " << name << " (not found)\n";
            continue;
        }
        std::string text = processClass(name , rd);
        specs[name] = {rd, text};
    }
    auto& SM = context.getSourceManager();
    std::set<unsigned> includeChecked;   // FileID hash values, dedupes per file
    for (const auto& [name, spec] : specs) {
        clang::SourceLocation afterBrace = spec.rd->getSourceRange().getEnd();
        clang::SourceLocation insertLoc = afterBrace;
        if (auto next = clang::Lexer::findNextToken(afterBrace, SM, context.getLangOpts())) {
            if (next->is(clang::tok::semi)) {
                insertLoc = next->getEndLoc();
            }
        }
        rewriter.InsertTextAfterToken(insertLoc, "\n" + spec.text);

        // Ensure the file owning this spec pulls in the persistent library.
        clang::FileID fid = SM.getFileID(insertLoc);
        if (includeChecked.insert(fid.getHashValue()).second) {
            if (!utils::fileIncludes(SM, fid, "persistenttype.hpp")) {
                rewriter.InsertText(SM.getLocForStartOfFile(fid),
                                    "#include \"persistenttype.hpp\"\n\n");
            }
        }
    }

    utils::writeAllRewrites(rewriter, context);
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
        llvm::errs() << "error: no __pers_root struct found in translation unit\n";
        return;
    }
    for(const auto* field : root->fields()) {
        if (auto t = utils::unwrapPersistentType(field->getType(), printPolicy)) {
            specializeType(*t);
        } else {
            llvm::outs() << " field " << field->getNameAsString() << " has non-persistent type "
                         << field->getType().getAsString(printPolicy) << "\n";
        }
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
        std::string wrapped = utils::wrapFieldType(field->getType(), printPolicy, seen, workList);
        b.fields += "    " + wrapped + " " + field->getNameAsString() + ";\n";
    }

     // Operator new/delete — always public.
    pub.methods += "    static void* operator new(std::size_t sz) { return pmem_alloc(sz, alignof(" + name + ")); }\n";
    pub.methods += "    static void  operator delete(void* p)     { pmem_free(p); }\n";

                            
    for(const auto* method : rd->methods()) {
        if(method->isImplicit()) continue;
        if(method->isOverloadedOperator()) continue; // TODO 
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
            llvm::errs() << "error: base-class initializer in constructor of "
                         << ctor->getParent()->getNameAsString()
                         << " is not supported (no inheritance in persistent<T>)\n";
            continue;
        }
        if(init->isIndirectMemberInitializer()) {
            llvm::errs() << "error: initializer of anonymous struct/union member in constructor of "
                         << ctor->getParent()->getNameAsString() << " is not supported\n";
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

    std::string sig = utils::buildMethodSignature(ctor, printPolicy);
    if(!initList.empty()) sig += " : " + initList;
    return sig + " " + wrapInTransaction(body, ctor) + "\n";
}

void RecursePersistentTyper::runRewriters(clang::Stmt* s, clang::Rewriter& scratch) {
    NewExprRewriter v1(printPolicy, this, scratch);
    v1.TraverseStmt(s);

    LocalLHSRewriter v2(printPolicy, scratch);
    v2.TraverseStmt(s);

    PmemPtrInterfaceRewriter v3(printPolicy, scratch);
    v3.TraverseStmt(s);

    CStringIdiomRewriter v4(context, scratch);
    v4.TraverseStmt(s);
}

std::string RecursePersistentTyper::processMethod(const clang::CXXMethodDecl* method) {
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
        std::string retType = method->getReturnType().getAsString(printPolicy);
        body = "{\n        " + retType + " __pers_ret = {};\n"
             + "        pmem::obj::transaction::run(pmem_pool(), [&]() {\n"
             + "            __pers_ret = [&]() -> " + retType + " " + body + "();\n"
             + "        });\n"
             + "        return __pers_ret;\n    }";
    }

    return body;
}
