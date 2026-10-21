#include "utils.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <clang/Basic/FileEntry.h>
#include <llvm/ADT/RewriteBuffer.h>
#include <llvm/Support/FileSystem.h>
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
        if(ctsd->getSpecializedTemplate()->getNameAsString() != "persistent")continue;

        const auto& args = ctsd->getTemplateArgs();
        if(args.size() == 0) continue;
        if(args[0].getKind() != clang::TemplateArgument::Type) continue;
        out.insert(args[0].getAsType().getAsString(policy));
    }
    return out;
}

namespace {
    void collectCXXRecords(const clang::DeclContext* dc, 
                                const std::string& name,
                                std::vector<const clang::CXXRecordDecl*>& out) {
        for(const auto* d : dc->decls()){
            if(const auto* rd = llvm::dyn_cast<clang::CXXRecordDecl>(d)) {
                if(!rd->getDescribedClassTemplate() &&
                rd->isCompleteDefinition() &&
                (rd->getQualifiedNameAsString() == name || rd->getNameAsString() == name)) {
                    out.push_back(rd);
                }
            }
            if(const auto* nested = llvm::dyn_cast<clang::DeclContext>(d)) {
                collectCXXRecords(nested, name, out);
            }
        }
    }

    std::vector<const clang::CXXRecordDecl*>
    findCXXRecordDeclsByName(clang::ASTContext& context, const std::string& name) {
        std::vector<const clang::CXXRecordDecl*> out;
        collectCXXRecords(context.getTranslationUnitDecl(), name, out);
        return out;
    }
}

const clang::CXXRecordDecl*
findCXXRecordDeclByName(clang::ASTContext& context, const std::string& name) {
    auto matches = findCXXRecordDeclsByName(context, name);
    return matches.empty() ? nullptr : matches.front();
}


std::optional<std::string>
unwrapPersistentType(clang::QualType qt,
                     const clang::PrintingPolicy& policy)
{
    const auto* rd = qt->getAsCXXRecordDecl();
    if (!rd) return std::nullopt;
    const auto* ctsd =
        llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rd);
    if (!ctsd) return std::nullopt;

    std::string tmplName = ctsd->getSpecializedTemplate()->getNameAsString();

    if (tmplName == "persistent") {
        // Direct `persistent<T>` — return T.
        return ctsd->getTemplateArgs()[0].getAsType().getAsString(policy);
    }
    if (tmplName == "pmem_ptr") {
        // `pmem_ptr<persistent<T>>` — recurse into the template arg.
        clang::QualType inner = ctsd->getTemplateArgs()[0].getAsType();
        return unwrapPersistentType(inner, policy);
    }
    return std::nullopt;
}


std::string
wrapFieldType(clang::QualType qt,
              const clang::PrintingPolicy& policy,
              std::set<std::string>& typesToSpecialize,
              std::vector<std::string>& workList)
{
    // Already persistence-aware (pmem_string, pmem_ptr<..>, persistent<..>):
    // emit unchanged. Keeps the pass idempotent at the field level, the same
    // property findExistingPersistentSpecs gives at the class level.
    if (isPersistenceAwareType(qt)) {
        return qt.getAsString(policy);
    }

    // char* / const char* / std::string -> pmem_string. Must precede the
    // pointer branch below, since char* is a pointer type.
    if (isStringLikeType(qt)) {
        return "pmem_string";
    }

    // Pointer to a user-defined class: wrap as pmem_ptr<persistent<U>>.
    if (qt->isPointerType()) {
        clang::QualType pointee = qt->getPointeeType();
        if (pointee->getAsCXXRecordDecl()) {
            std::string innerName = pointee.getAsString(policy);
            if (typesToSpecialize.insert(innerName).second) {
                workList.push_back(innerName);
            }
            return "pmem_ptr<persistent<" + innerName + ">>";
        }
        // Pointer to primitive (e.g. int*) — library's primitive spec
        // already accepts pointer types, so persistent<int*> works as-is.
        return "persistent<" + qt.getAsString(policy) + ">";
    }

    // Fundamental type: wrap as persistent<F>.
    if (qt->isFundamentalType()) {
        return "persistent<" + qt.getAsString(policy) + ">";
    }

    // Class type by value: wrap as persistent<Class>, queue Class.
    if (qt->getAsCXXRecordDecl()) {
        std::string innerName = qt.getAsString(policy);
        if (typesToSpecialize.insert(innerName).second) {
            workList.push_back(innerName);
        }
        return "persistent<" + innerName + ">";
    }

    // Anything else: leave the type alone but mark it so the user can spot it
    // in the generated output.
    return qt.getAsString(policy) + "  /* unwrapped */";
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
        sig += method->getReturnType().getAsString(policy) + " " + method->getNameAsString();
    }

    sig+= "(";
    bool first = true;
    for(const auto* param : method->parameters()) {
        if(!first) sig += ", ";
        sig += param->getType().getAsString(policy);
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

bool isUserClassPointerType(clang::QualType qt) {
    if (!qt->isPointerType()) return false;
    return qt->getPointeeType()->getAsCXXRecordDecl() != nullptr;
}

bool isStringLikeType(clang::QualType qt) {
    if (qt->isPointerType()) {
        // char* / const char* — qualifiers live on the QualType, so the
        // underlying Type is plain `char` either way.
        return qt->getPointeeType()->isCharType();
    }
    // std::string is std::basic_string<char, char_traits<char>, allocator<char>>.
    const auto* rd = qt->getAsCXXRecordDecl();
    if (!rd) return false;
    const auto* ctsd = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rd);
    if (!ctsd) return false;
    if (ctsd->getSpecializedTemplate()->getNameAsString() != "basic_string") return false;
    const auto& args = ctsd->getTemplateArgs();
    return args.size() > 0
        && args[0].getKind() == clang::TemplateArgument::Type
        && args[0].getAsType()->isCharType();
}

bool isPersistenceAwareType(clang::QualType qt) {
    const auto* rd = qt->getAsCXXRecordDecl();
    if (!rd) return false;
    if (rd->getNameAsString() == "pmem_string") return true;
    const auto* ctsd = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rd);
    if (!ctsd) return false;
    const std::string tmpl = ctsd->getSpecializedTemplate()->getNameAsString();
    return tmpl == "persistent" || tmpl == "pmem_ptr";
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


