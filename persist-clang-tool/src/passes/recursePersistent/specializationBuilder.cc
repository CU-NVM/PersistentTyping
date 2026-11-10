#include "specializationBuilder.h"
#include <llvm/Support/raw_ostream.h>
#include "../../utils/utils.h"
#include "../../utils/typeTranslation.h"
#include <clang/AST/DeclTemplate.h>
#include <clang/Lex/Lexer.h>

SpecializationBuilder::SpecializationBuilder(clang::ASTContext &context,
                                             clang::Rewriter &rewriter)
    : context(context),
      rewriter(rewriter),
      printPolicy(context.getPrintingPolicy()),
      memberPersistifier(context, printPolicy, [this](const std::string& c) { queueSpecialization(c); }) {
    // memberPersistifier holds printPolicy by reference, so it sees this too.
    printPolicy.SuppressTagKeyword = true;
}

void SpecializationBuilder::run() {
    llvm::outs() << "Starting SpecializationBuilder\n";
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
        std::string text = buildSpecialization(name , rd);
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
clang::SourceLocation SpecializationBuilder::specInsertLoc(const clang::CXXRecordDecl* rd) {
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
void SpecializationBuilder::markTransformed(clang::FileID fid, std::set<unsigned>& marked) {
    if (!marked.insert(fid.getHashValue()).second) return;
    auto& SM = context.getSourceManager();
    if (utils::hasTransformedDefine(SM.getBufferData(fid))) return;
    rewriter.InsertText(SM.getLocForStartOfFile(fid), utils::TRANSFORMED_DEFINE,
                        /*InsertAfter=*/false);
}

void SpecializationBuilder::queueSpecialization(const std::string& typeName) {
    if(existingSpecs.count(typeName)) return;
    if(!seen.insert(typeName).second) return;
    workList.push_back(typeName);
}

void SpecializationBuilder::discoverTypesFromRoot() {
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
        for (const std::string& name : tt.classes) queueSpecialization(name);
    }
}

const std::map<std::string, SpecializationBuilder::Spec>&
SpecializationBuilder::getSpecs() const {
    return specs;
}

std::string SpecializationBuilder::buildSpecialization(const std::string& name,
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
                  + memberPersistifier.persistifyDefaultMemberInit(field) + ";\n";
        for (const std::string& c : reached) queueSpecialization(c);
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
        b.methods += "    " + buildMethod(method) + "\n";
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

// A class a method takes or returns by pointer or reference is spelled
// persistent<C> in the signature, so it needs a specialisation too.
void SpecializationBuilder::queueSignatureClasses(const clang::CXXMethodDecl* method) {
    std::vector<clang::QualType> types = {method->getReturnType()};
    for (const auto* param : method->parameters()) types.push_back(param->getType());
    for (clang::QualType t : types) {
        utils::TypeTranslation tt = utils::translateDramRef(t, printPolicy);
        if (!tt.ok()) continue;
        for (const std::string& c : tt.classes) queueSpecialization(c);
    }
}

std::string SpecializationBuilder::buildMethod(const clang::CXXMethodDecl* method) {
    queueSignatureClasses(method);
    std::string sig = utils::buildMethodSignature(method, printPolicy);

    if(method->isDefaulted() && !method->doesThisDeclarationHaveABody())
        return sig + " = default;";
    if(method->isDeletedAsWritten())
        return sig + " = delete;";
    if(!method->hasBody())
        return sig + ";";

    return sig + memberPersistifier.persistifyDefinition(method) + "\n";
}
