#include "utils.h"
#include "typeTranslation.h"
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


