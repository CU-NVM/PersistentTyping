#include "utils.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <algorithm>
#include <clang/Basic/FileEntry.h>
#include <llvm/ADT/RewriteBuffer.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <clang/Lex/Lexer.h>


namespace fs = std::filesystem;
namespace utils
{

bool fileExists(const std::string &file)
{
    return std::ifstream(file).good();
}

std::set<std::string> findExistingPersistentSpecs(clang::ASTContext& context,
                                                  const clang::PrintingPolicy& policy){
    std::set<std::string> out;
    auto* tu = context.getTranslationUnitDecl();
    for (const auto* decl : tu->decls()) {
        const auto* ctsd = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
        if(!ctsd) continue;
        if(llvm::isa<clang::ClassTemplatePartialSpecializationDecl>(ctsd)) continue;
        if(!ctsd->isCompleteDefinition()) continue;
        // Only a `template<> class persistent<C>` someone wrote or the tool
        // generated; never an instantiation of the library's partial
        // specialisations, e.g. pmem_ptr<persistent<C>>.
        if(ctsd->getSpecializationKind() != clang::TSK_ExplicitSpecialization) continue;
        if(ctsd->getSpecializedTemplate()->getNameAsString() != "persistent")continue;

        const auto& args = ctsd->getTemplateArgs();
        if(args.size() == 0) continue;
        if(args[0].getKind() != clang::TemplateArgument::Type) continue;
        clang::QualType arg = args[0].getAsType();
        if(const auto* td = arg->getAsTagDecl())
            out.insert(className(td, policy));
        else
            out.insert(arg.getAsString(policy));
    }
    return out;
}

namespace {
    void collectCXXRecords(const clang::DeclContext* dc,
                                const std::string& name,
                                const clang::PrintingPolicy& policy,
                                std::vector<const clang::CXXRecordDecl*>& out) {
        for(const auto* d : dc->decls()){
            if(const auto* rd = llvm::dyn_cast<clang::CXXRecordDecl>(d)) {
                if(!rd->getDescribedClassTemplate() &&
                rd->isCompleteDefinition() &&
                (className(rd, policy) == name ||
                 rd->getQualifiedNameAsString() == name || rd->getNameAsString() == name)) {
                    out.push_back(rd);
                }
            }
            if(const auto* nested = llvm::dyn_cast<clang::DeclContext>(d)) {
                collectCXXRecords(nested, name, policy, out);
            }
        }
    }

    std::vector<const clang::CXXRecordDecl*>
    findCXXRecordDeclsByName(clang::ASTContext& context, const std::string& name,
                             const clang::PrintingPolicy& policy) {
        std::vector<const clang::CXXRecordDecl*> out;
        collectCXXRecords(context.getTranslationUnitDecl(), name, policy, out);
        return out;
    }
}

const clang::CXXRecordDecl*
findCXXRecordDeclByName(clang::ASTContext& context, const std::string& name,
                        const clang::PrintingPolicy& policy) {
    auto matches = findCXXRecordDeclsByName(context, name, policy);
    return matches.empty() ? nullptr : matches.front();
}

bool isRootCandidate(const clang::VarDecl* vd) {
    // One copy for the whole program: globals, namespace-scope variables and
    // static locals. Static data members also have static storage, but a
    // root inside a class is an error (closure doc 6.5), not a root.
    return vd->getStorageDuration() == clang::SD_Static && !vd->isStaticDataMember();
}

namespace {
    // The library's persistent class template, as opposed to a user template
    // that happens to share its name.
    bool isPersistentTypeTemplate(const clang::TemplateDecl* td) {
        return td && td->getName() == "persistent"
            && td->getDeclContext()->getRedeclContext()->isTranslationUnit();
    }
}

const clang::ClassTemplateSpecializationDecl* persistentSpecialization(clang::QualType t) {
    const auto* ctsd = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
        t.getCanonicalType()->getAsCXXRecordDecl());
    if (!ctsd || !isPersistentTypeTemplate(ctsd->getSpecializedTemplate())) return nullptr;
    return ctsd;
}

clang::QualType extractTargetFromPersistentType(clang::QualType t) {
    const auto* ctsd = persistentSpecialization(t);
    if (!ctsd) return {};
    return ctsd->getTemplateArgs()[0].getAsType().getCanonicalType();
}

clang::SourceRange initArgsRange(const clang::CXXNewExpr* newExpr) {
    // `new T(args)` has a direct-init range around the parentheses;
    // `new T{args}` has an initializer list instead.
    clang::SourceRange range = newExpr->getDirectInitRange();
    if (newExpr->getInitializationStyle() == clang::CXXNewInitializationStyle::Braces) {
        // For a class the list becomes a constructor call, whose own range
        // starts at the type; its brace range is the `{...}` alone.
        const clang::Expr* init = newExpr->getInitializer();
        if (const auto* ce = llvm::dyn_cast_or_null<clang::CXXConstructExpr>(init))
            range = ce->getParenOrBraceRange();
        else if (const auto* ile = llvm::dyn_cast_or_null<clang::InitListExpr>(init))
            range = {ile->getLBraceLoc(), ile->getRBraceLoc()};
    }
    return range;
}

std::string extractInitArgs(const clang::CXXNewExpr* newExpr,
                            clang::ASTContext& context)
{
    clang::SourceRange range = initArgsRange(newExpr);
    if (range.isInvalid()) return "";
    std::string text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(range),
        context.getSourceManager(),
        context.getLangOpts()).str();

    if (text.size() >= 2 && ((text.front() == '(' && text.back() == ')')
                             || (text.front() == '{' && text.back() == '}'))) {
        return text.substr(1, text.size() - 2);
    }
    return "";
}


namespace {
    clang::PrintingPolicy namePolicy(clang::PrintingPolicy p) {
        p.SuppressTagKeyword = true;
        p.SuppressUnwrittenScope = true;
        return p;
    }

    // One run of P over a type. Each step works on the canonical type, so
    // typedefs, `using` aliases and pmem_ptr<X> (= persistent<X*>) all reach
    // the same branch as the type they name.
    class Translator {
    public:
        Translator(const clang::PrintingPolicy& policy, TypeTranslation& out, bool aliasPointers)
            : policy(namePolicy(policy)), out(out), aliasPointers(aliasPointers) {}

        bool run(clang::QualType t, std::string& prefix, std::string& suffix) {
            t = t.getCanonicalType();
            clang::Qualifiers quals = t.getLocalQualifiers();
            if (quals.hasVolatile())
                return fail(t, "volatile has no persistent form");
            if (quals.hasConst()) {
                quals.removeConst();
                if (!quals.empty())
                    return fail(t, "qualifier has no persistent form");
                if (!run(t.getLocalUnqualifiedType(), prefix, suffix)) return false;
                prefix = "const " + prefix;
                return true;
            }
            if (!quals.empty())
                return fail(t, "qualifier has no persistent form");

            const clang::Type* ty = t.getTypePtr();
            if (ty->isReferenceType())
                return fail(t, "references cannot live in pmem; use a pointer");
            if (ty->isMemberPointerType())
                return fail(t, "pointers to members have no persistent form");

            if (const auto* pt = llvm::dyn_cast<clang::PointerType>(ty)) {
                clang::QualType pointee = pt->getPointeeType();
                if (pointee->isVoidType())
                    return fail(t, "void* has no pointee type to keep in pmem");
                if (pointee->isFunctionType())
                    return fail(t, "function pointers cannot live in pmem");
                if (pointee->isArrayType())
                    return fail(t, "pointers to arrays are not supported; point to the element type");
                std::string inner, innerSuffix;
                if (!run(pointee, inner, innerSuffix)) return false;
                prefix = aliasPointers ? "pmem_ptr<" + inner + innerSuffix + ">"
                                       : "persistent<" + inner + innerSuffix + "*>";
                suffix.clear();
                return true;
            }

            if (const auto* at = llvm::dyn_cast<clang::ConstantArrayType>(ty)) {
                if (!run(at->getElementType(), prefix, suffix)) return false;
                suffix = "[" + std::to_string(at->getSize().getZExtValue()) + "]" + suffix;
                return true;
            }
            if (ty->isArrayType())
                return fail(t, "arrays need a constant size to live in pmem");

            if (const auto* et = llvm::dyn_cast<clang::EnumType>(ty)) {
                prefix = "persistent<" + className(et->getDecl(), policy) + ">";
                suffix.clear();
                return true;
            }
            if (const auto* bt = llvm::dyn_cast<clang::BuiltinType>(ty)) {
                if (!bt->isInteger() && !bt->isFloatingPoint())
                    return fail(t, "has no persistent form");
                prefix = "persistent<" + t.getAsString(policy) + ">";
                suffix.clear();
                return true;
            }
            if (const auto* rd = ty->getAsCXXRecordDecl())
                return runClass(t, rd, prefix, suffix);

            return fail(t, "has no persistent form");
        }

    private:
        clang::PrintingPolicy policy;
        TypeTranslation& out;
        bool aliasPointers;

        bool fail(clang::QualType t, const std::string& why) {
            out.error = "'" + t.getAsString(policy) + "': " + why;
            return false;
        }

        bool runClass(clang::QualType t, const clang::CXXRecordDecl* rd,
                      std::string& prefix, std::string& suffix) {
            if (rd->isUnion())
                return fail(t, "unions cannot live in pmem");
            if (clang::QualType x = extractTargetFromPersistentType(t); !x.isNull())
                return run(x, prefix, suffix);
            const clang::SourceManager& SM = rd->getASTContext().getSourceManager();
            if (rd->isInStdNamespace() || SM.isInSystemHeader(rd->getLocation()))
                return fail(t, "library types cannot live in pmem");
            if (llvm::isa<clang::ClassTemplateSpecializationDecl>(rd))
                return fail(t, "class templates are not supported yet");
            if (!rd->getIdentifier() || rd->isLambda())
                return fail(t, "unnamed classes have no persistent<> name");
            if (rd->isLocalClass())
                return fail(t, "classes defined inside a function cannot be specialised");
            if (const clang::CXXRecordDecl* def = rd->getDefinition()) {
                if (def->getNumBases() > 0)
                    return fail(t, "inheritance is not supported yet");
                if (def->isPolymorphic())
                    return fail(t, "classes with virtual methods are not supported");
            }
            std::string name = className(rd, policy);
            if (std::find(out.classes.begin(), out.classes.end(), name) == out.classes.end())
                out.classes.push_back(name);
            prefix = "persistent<" + name + ">";
            suffix.clear();
            return true;
        }
    };
}

std::string className(const clang::TagDecl* td, const clang::PrintingPolicy& policy) {
    const clang::ASTContext& ctx = td->getASTContext();
    return ctx.getTypeDeclType(td).getCanonicalType().getAsString(namePolicy(policy));
}

TypeTranslation translateType(clang::QualType qt, const clang::PrintingPolicy& policy,
                              bool aliasPointers) {
    TypeTranslation out;
    if (!Translator(policy, out, aliasPointers).run(qt, out.prefix, out.suffix)) {
        out.prefix.clear();
        out.suffix.clear();
        out.classes.clear();
    }
    return out;
}

TypeTranslation translateDramRef(clang::QualType qt, const clang::PrintingPolicy& policy) {
    clang::QualType c = qt.getCanonicalType();
    const char* sigil = c->isPointerType()           ? "*"
                      : c->isLValueReferenceType()  ? "&"
                      : c->isRValueReferenceType()  ? "&&" : nullptr;
    TypeTranslation out;
    if (!sigil) {
        out.error = "'" + qt.getAsString(policy) + "' is not a pointer or reference";
        return out;
    }
    out = translateType(c->getPointeeType(), policy);
    if (!out.ok()) return out;
    if (!out.suffix.empty()) {
        out = TypeTranslation{};
        out.error = "'" + qt.getAsString(policy) + "': pointers to arrays are not supported";
        return out;
    }
    out.prefix += sigil;
    if (c.isLocalConstQualified()) out.prefix += " const";
    return out;
}

std::string signatureType(clang::QualType qt, const clang::PrintingPolicy& policy) {
    TypeTranslation tt = translateDramRef(qt, policy);
    if (tt.ok() && !tt.classes.empty()) return tt.prefix;
    return qt.getAsString(policy);
}

std::string
wrapFieldType(const clang::FieldDecl* field,
              const clang::PrintingPolicy& policy,
              std::vector<std::string>& reached)
{
    std::string name = field->getNameAsString();
    TypeTranslation tt = translateType(field->getType(), policy);
    if (tt.ok() && field->isBitField())
        tt.error = "bit-fields cannot be wrapped in persistent<>";
    if (!tt.ok()) {
        const clang::SourceManager& SM = field->getASTContext().getSourceManager();
        reportError(field->getLocation().printToString(SM) + ": field '"
                    + field->getParent()->getNameAsString() + "::" + name + "': " + tt.error);
        return field->getType().getAsString(policy) + " " + name;
    }
    reached.insert(reached.end(), tt.classes.begin(), tt.classes.end());
    return tt.declare(name);
}


std::string
buildMethodSignature(const clang::CXXMethodDecl* method,
                     const clang::PrintingPolicy& policy)
{
    std::string sig;
    if (method->isStatic()) sig+= "static ";

    if(llvm::isa<clang::CXXConstructorDecl>(method)) {
        sig += "persistent";
    }else if(llvm::isa<clang::CXXDestructorDecl>(method)) {
        sig += "~persistent";
    } else {
        sig += signatureType(method->getReturnType(), policy) + " " + method->getNameAsString();
    }

    sig+= "(";
    bool first = true;
    for(const auto* param : method->parameters()) {
        if(!first) sig += ", ";
        sig += signatureType(param->getType(), policy);
        if(!param->getNameAsString().empty()) {
            sig += " " + param->getNameAsString();
        }
        // Default arguments live only on the in-class declaration, which is
        // the one rd->methods() hands us. Copied as written, not rewritten.
        if(param->hasDefaultArg() && !param->hasUnparsedDefaultArg()
                                  && !param->hasUninstantiatedDefaultArg()) {
            const clang::ASTContext& ctx = param->getASTContext();
            sig += " = " + clang::Lexer::getSourceText(
                               clang::CharSourceRange::getTokenRange(param->getDefaultArgRange()),
                               ctx.getSourceManager(), ctx.getLangOpts()).str();
        }
        first = false;
    }
    sig += ")";
    if(method->isConst()) sig += " const";
    return sig;
}


std::string
getMethodBodyText(const clang::CXXMethodDecl* method, clang::ASTContext& context){
    if(!method->hasBody()) return "";
    clang::SourceRange bodyRange = method->getBody()->getSourceRange();
    return clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(bodyRange),
                                    context.getSourceManager(),
                                    context.getLangOpts()).str();
                    
}

std::string fileOf(const clang::SourceManager& SM, clang::SourceLocation loc) {
    auto fileRef = SM.getFileEntryRefForID(SM.getFileID(loc));
    if (!fileRef) return "<unknown>";
    llvm::SmallString<256> real;
    if (llvm::sys::fs::real_path(fileRef->getName(), real)) return fileRef->getName().str();
    return real.str().str();
}

std::string locationOf(const clang::SourceManager& SM, clang::SourceLocation loc) {
    return fileOf(SM, loc) + ":" + std::to_string(SM.getSpellingLineNumber(loc))
         + ":" + std::to_string(SM.getSpellingColumnNumber(loc));
}

bool applyEdits(const std::string& file, std::vector<TextEdit> edits,
                std::string& result, std::vector<std::string>& errors) {
    auto buf = llvm::MemoryBuffer::getFile(file);
    if (!buf) {
        errors.push_back("cannot read " + file + ": " + buf.getError().message());
        return false;
    }
    llvm::StringRef code = (*buf)->getBuffer();
    std::stable_sort(edits.begin(), edits.end(),
                     [](const TextEdit& a, const TextEdit& b) { return a.offset < b.offset; });

    unsigned pos = 0;
    for (const TextEdit& e : edits) {
        if (e.offset < pos || e.offset + e.length > code.size()) {
            errors.push_back("overlapping or out-of-range rewrite at offset "
                             + std::to_string(e.offset) + " of " + file);
            return false;
        }
        result += code.slice(pos, e.offset).str();
        result += e.text;
        pos = e.offset + e.length;
    }
    result += code.substr(pos).str();
    return true;
}

bool writeFile(const std::string& file, const std::string& text) {
    std::error_code EC;
    llvm::raw_fd_ostream out(file, EC, llvm::sys::fs::OF_Text);
    if (EC) {
        llvm::errs() << "error: cannot write " << file << ": " << EC.message() << "\n";
        return false;
    }
    out << text;
    llvm::outs() << "wrote rewritten source to " << file << "\n";
    return true;
}

namespace {
    unsigned errors = 0;
}

void reportError(const std::string& what) {
    llvm::errs() << "error: " << what << "\n";
    ++errors;
}

unsigned errorCount() { return errors; }

const char* const TRANSFORMED_DEFINE =
    "#ifndef PERSIST_TRANSFORMED\n#define PERSIST_TRANSFORMED\n#endif\n";

bool hasTransformedDefine(llvm::StringRef text) {
    return text.starts_with(TRANSFORMED_DEFINE);
}

void writeAllRewrites(clang::Rewriter& rewriter, clang::ASTContext& context) {
    auto& SM = context.getSourceManager();
    bool wroteAny = false;
    for (auto it = rewriter.buffer_begin(); it != rewriter.buffer_end(); ++it) {
        clang::FileID fid = it->first;
        const llvm::RewriteBuffer& buf = it->second;

        auto fileRef = SM.getFileEntryRefForID(fid);
        if (!fileRef) {
            llvm::errs() << "warning: skipping rewrite buffer with no file entry\n";
            continue;
        }
        std::string fileName = fileRef->getName().str();
        std::error_code EC;
        llvm::raw_fd_ostream out(fileName, EC, llvm::sys::fs::OF_Text);
        if (EC) {
            llvm::errs() << "error opening " << fileName << ": " << EC.message() << "\n";
            continue;
        }
        buf.write(out);
        llvm::outs() << "wrote rewritten source to " << fileName << "\n";
        wroteAny = true;
    }
    if (!wroteAny) {
        llvm::errs() << "warning: no rewrites recorded; nothing to write\n";
    }
}

bool fileIncludes(clang::SourceManager& SM,
                  clang::FileID fid,
                  const std::string& header)
{
    auto buf = SM.getBufferOrNone(fid);
    if (!buf) return false;
    llvm::StringRef text = buf->getBuffer();
    std::string quoted = "#include \"" + header + "\"";
    std::string angled = "#include <" + header + ">";
    return text.contains(quoted) || text.contains(angled);
}

}   // namespace utils


