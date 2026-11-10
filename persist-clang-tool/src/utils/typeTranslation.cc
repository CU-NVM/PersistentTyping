#include "typeTranslation.h"
#include "utils.h"
#include <algorithm>
#include <clang/Lex/Lexer.h>

namespace utils {

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

}   // namespace utils
