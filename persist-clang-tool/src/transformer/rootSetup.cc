#include "rootSetup.h"
#include "../utils/utils.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

// ---- RootTable: merging the declarations of every TU ----

namespace {
    const char* linkageName(RootLinkage l) {
        switch (l) {
            case RootLinkage::External:  return "external";
            case RootLinkage::Internal:  return "static";
            case RootLinkage::MainLocal: return "main-local";
        }
        return "?";
    }

    std::map<std::string, std::vector<const RootDeclRecord*>>
    groupByKey(const std::map<std::string, RootDeclRecord>& decls) {
        std::map<std::string, std::vector<const RootDeclRecord*>> merged;
        for (const auto& [loc, d] : decls)
            merged[RootTable::mergeKey(d)].push_back(&d);
        return merged;
    }

    // `static` roots from different files can share a name, so their field is
    // prefixed with the file: a.cpp's `counter` becomes `a__counter`.
    std::string fieldName(const RootDeclRecord& d) {
        if (d.linkage != RootLinkage::Internal) return d.varName;
        std::string stem = llvm::sys::path::stem(d.file).str();
        for (char& c : stem)
            if (!isalnum(static_cast<unsigned char>(c))) c = '_';
        return stem + "__" + d.varName;
    }
}

std::string RootTable::mergeKey(const RootDeclRecord& d) {
    return mergeKey(d.linkage, d.file, d.varName);
}

std::string RootTable::mergeKey(RootLinkage linkage, const std::string& file,
                                const std::string& varName) {
    if (linkage == RootLinkage::External) return varName;
    return file + "::" + varName;
}

std::vector<MergedRoot> RootTable::merge(std::vector<std::string>& errors) const {
    std::vector<MergedRoot> roots;
    std::map<std::string, std::string> fieldOwner;   // field -> key that took it

    for (const auto& [key, group] : groupByKey(decls)) {
        const RootDeclRecord& first = *group.front();
        unsigned defs = 0;
        for (const RootDeclRecord* d : group) {
            defs += d->isDefinition;
            if (d->typeName != first.typeName)
                errors.push_back("root '" + key + "' is persistent<" + first.typeName + "> at "
                                 + first.location + " but persistent<" + d->typeName
                                 + "> at " + d->location);
            if (!d->isPointer)
                errors.push_back("root '" + key + "' at " + d->location
                                 + " is not a pointer; only persistent<T>* roots are supported");
        }
        if (defs == 0)
            errors.push_back("root '" + key + "' is declared at " + first.location
                             + " but not defined in any of the given files");
        else if (defs > 1)
            errors.push_back("root '" + key + "' is defined " + std::to_string(defs) + " times");

        auto type = types.find(first.typeName);
        if (type != types.end() && !type->second.error.empty())
            errors.push_back("root '" + key + "': " + type->second.error);

        std::string field = fieldName(first);
        auto [owner, inserted] = fieldOwner.emplace(field, key);
        if (!inserted)
            errors.push_back("roots '" + owner->second + "' and '" + key
                             + "' both map to field '" + field + "'");
        roots.push_back({key, field, first.typeName});
    }
    return roots;
}

void RootTable::print(llvm::raw_ostream& out) const {
    auto merged = groupByKey(decls);
    out << "== roots: " << merged.size() << " ==\n";
    for (const auto& [key, group] : merged) {
        const RootDeclRecord& first = *group.front();
        out << "\n" << key << "  [" << linkageName(first.linkage) << "]\n"
            << "  field: pmem_ptr<persistent<" << first.typeName << ">> "
            << fieldName(first) << ";\n";

        for (const RootDeclRecord* d : group)
            out << "  " << (d->isDefinition ? "definition " : "declaration")
                << " persistent<" << d->typeName << ">" << (d->isPointer ? "*" : "")
                << "  at " << d->location << "\n";

        for (const auto& [loc, s] : initSites) {
            if (s.rootKey != key) continue;
            out << "  init site at " << s.location
                << "  (offset " << s.offset << ", length " << s.length << ")"
                << (s.initArgs.empty() ? "" : "  args=`" + s.initArgs + "`") << "\n";
        }
    }

    out << "\nmain() in:";
    for (const auto& [f, offset] : mainFiles) out << " " << f;
    if (mainFiles.empty()) out << " (none)";
    out << "\n";
}

// ---- RootCollector: recording one TU into the table ----

namespace {
    // Real path of the file `loc` sits in, so a header reached through two
    // different -I paths still gets one name.
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

    RootLinkage linkageOf(const clang::VarDecl* vd) {
        if (llvm::isa<clang::FunctionDecl>(vd->getDeclContext())) return RootLinkage::MainLocal;
        if (vd->hasExternalFormalLinkage()) return RootLinkage::External;
        return RootLinkage::Internal;
    }

    // The T of a `persistent<T>` or `persistent<T>*` already accepted by
    // utils::tryExtractPersistentTypeName.
    clang::QualType persistentArg(clang::QualType qt) {
        if (qt->isPointerType()) qt = qt->getPointeeType();
        const auto* ctsd = llvm::cast<clang::ClassTemplateSpecializationDecl>(qt->getAsCXXRecordDecl());
        return ctsd->getTemplateArgs()[0].getAsType();
    }

    class RootCollectVisitor : public clang::RecursiveASTVisitor<RootCollectVisitor> {
    public:
        RootCollectVisitor(RootTable& table, const clang::PrintingPolicy& policy,
                           clang::ASTContext& context)
            : table(table), printPolicy(policy), context(context),
              SM(context.getSourceManager()) {}

        bool VisitFunctionDecl(clang::FunctionDecl* fd) {
            if (fd->isMain() && fd->doesThisDeclarationHaveABody()) {
                clang::SourceLocation begin = SM.getFileLoc(fd->getBeginLoc());
                table.mainFiles[fileOf(SM, begin)] = SM.getFileOffset(begin);
            }
            return true;
        }

        bool VisitVarDecl(clang::VarDecl* vd) {
            if (!utils::isRootCandidate(vd)) return true;
            auto typeName = utils::tryExtractPersistentTypeName(vd->getType(), printPolicy);
            if (!typeName) return true;
            clang::SourceLocation loc = SM.getFileLoc(vd->getLocation());
            if (SM.isInSystemHeader(loc)) return true;

            std::string where = locationOf(SM, loc);
            table.decls.emplace(where, RootDeclRecord{
                *typeName, vd->getNameAsString(), vd->getType()->isPointerType(), linkageOf(vd),
                vd->isThisDeclarationADefinition() != clang::VarDecl::DeclarationOnly,
                fileOf(SM, loc), where
            });
            recordType(*typeName, persistentArg(vd->getType()));

            if (vd->hasInit()) {
                const clang::Expr* init = vd->getInit()->IgnoreImpCasts();
                if (const auto* newExpr = llvm::dyn_cast<clang::CXXNewExpr>(init))
                    recordInitSite(*typeName, vd, newExpr);
            }
            return true;
        }

        // `root = new persistent<T>(...)` outside the declaration.
        bool VisitBinaryOperator(clang::BinaryOperator* bo) {
            if (bo->getOpcode() != clang::BO_Assign) return true;
            const auto* dref = llvm::dyn_cast<clang::DeclRefExpr>(bo->getLHS()->IgnoreImpCasts());
            if (!dref) return true;
            const auto* vd = llvm::dyn_cast<clang::VarDecl>(dref->getDecl());
            if (!vd || !utils::isRootCandidate(vd)) return true;
            auto typeName = utils::tryExtractPersistentTypeName(vd->getType(), printPolicy);
            if (!typeName) return true;
            const auto* newExpr = llvm::dyn_cast<clang::CXXNewExpr>(bo->getRHS()->IgnoreImpCasts());
            if (!newExpr) return true;
            recordInitSite(*typeName, vd, newExpr);
            return true;
        }

    private:
        RootTable& table;
        const clang::PrintingPolicy& printPolicy;
        clang::ASTContext& context;
        const clang::SourceManager& SM;

        void recordInitSite(const std::string& typeName, const clang::VarDecl* vd,
                            const clang::CXXNewExpr* newExpr) {
            clang::SourceLocation begin = SM.getFileLoc(newExpr->getBeginLoc());
            clang::SourceLocation end = clang::Lexer::getLocForEndOfToken(
                SM.getFileLoc(newExpr->getEndLoc()), 0, SM, context.getLangOpts());
            if (SM.isInSystemHeader(begin)) return;
            unsigned offset = SM.getFileOffset(begin);
            std::string where = locationOf(SM, begin);
            std::string rootKey = RootTable::mergeKey(
                linkageOf(vd), fileOf(SM, SM.getFileLoc(vd->getLocation())), vd->getNameAsString());
            table.initSites.emplace(where, InitSiteRecord{
                typeName, vd->getNameAsString(), rootKey, fileOf(SM, begin), offset,
                SM.getFileOffset(end) - offset,
                utils::extractInitArgs(newExpr, context), where
            });
        }

        // Works out how the shared root header can name T: a forward
        // declaration when T is a plain class at namespace scope, otherwise
        // an include of the header that defines it.
        void recordType(const std::string& name, clang::QualType t) {
            if (table.types.count(name)) return;
            RootTypeInfo& info = table.types[name];
            info.qualifiedName = clang::TypeName::getFullyQualifiedName(t, context, printPolicy);
            const clang::TagDecl* td = t->getAsTagDecl();
            if (!td) return;    // builtin types need nothing
            const clang::TagDecl* def = td->getDefinition() ? td->getDefinition() : td;
            clang::SourceLocation loc = SM.getFileLoc(def->getLocation());

            if (auto fwd = forwardDeclFor(td)) {
                info.forwardDecl = *fwd;
            } else if (SM.isInSystemHeader(loc)) {
                info.error = name + " comes from a system header, which is not supported for a root yet";
            } else if (SM.getFileID(loc) == SM.getMainFileID()) {
                info.error = name + " is defined in " + fileOf(SM, loc)
                           + " and cannot be forward-declared in the shared root header; move it to a header";
            } else {
                info.include = fileOf(SM, loc);
            }
        }

        std::optional<std::string> forwardDeclFor(const clang::TagDecl* td) {
            const auto* rd = llvm::dyn_cast<clang::CXXRecordDecl>(td);
            if (!rd || rd->getName().empty()) return std::nullopt;
            if (llvm::isa<clang::ClassTemplateSpecializationDecl>(rd) || rd->getDescribedClassTemplate())
                return std::nullopt;
            std::string open, close;
            for (const clang::DeclContext* dc = rd->getDeclContext(); !dc->isTranslationUnit();
                 dc = dc->getParent()) {
                if (dc->isTransparentContext()) continue;   // extern "C++" { ... }
                const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(dc);
                if (!ns || ns->isAnonymousNamespace()) return std::nullopt;
                open = (ns->isInline() ? "inline namespace " : "namespace ")
                     + ns->getNameAsString() + " { " + open;
                close += " }";
            }
            const char* tag = rd->isClass() ? "class " : rd->isUnion() ? "union " : "struct ";
            return open + tag + rd->getNameAsString() + ";" + close;
        }
    };
}

RootCollector::RootCollector(clang::ASTContext &context, RootTable &table)
        : context(context), table(table), printPolicy(context.getPrintingPolicy()) {
            printPolicy.SuppressTagKeyword = true;
        }

void RootCollector::start() {
    RootCollectVisitor visitor(table, printPolicy, context);
    visitor.TraverseDecl(context.getTranslationUnitDecl());
}

// ---- emitRoots: writing the root header and rewriting the sources ----

namespace {
    // Text that replaces [offset, offset + length) of a file; length 0 inserts.
    struct TextEdit {
        unsigned offset;
        unsigned length;
        std::string text;
    };

    // How `fromFile` should spell `#include` of `target`: relative to the
    // including file's directory, which a quoted include searches first.
    std::string includeSpelling(const std::string& fromFile, const std::string& target) {
        std::error_code ec;
        auto rel = std::filesystem::relative(
            target, std::filesystem::path(fromFile).parent_path(), ec);
        return ec || rel.empty() ? target : rel.string();
    }

    std::string buildHeader(const RootTable& table, const std::vector<MergedRoot>& roots,
                            const std::string& headerPath) {
        std::set<std::string> includes, forwardDecls;
        for (const MergedRoot& r : roots) {
            auto type = table.types.find(r.typeName);
            if (type == table.types.end()) continue;
            if (!type->second.include.empty())
                includes.insert(includeSpelling(headerPath, type->second.include));
            if (!type->second.forwardDecl.empty())
                forwardDecls.insert(type->second.forwardDecl);
        }

        std::ostringstream out;
        out << "// Generated by persist-tool root-setup: the persistent root shared by every\n"
            << "// file of the program. Rerunning root-setup overwrites it.\n"
            << "#pragma once\n"
            << "#include \"persistenttype.hpp\"\n";
        for (const auto& inc : includes) out << "#include \"" << inc << "\"\n";
        out << "\n";
        for (const auto& fwd : forwardDecls) out << fwd << "\n";
        if (!forwardDecls.empty()) out << "\n";

        out << "struct __pers_root {\n";
        for (const MergedRoot& r : roots) {
            auto type = table.types.find(r.typeName);
            const std::string& name = type != table.types.end() && !type->second.qualifiedName.empty()
                                    ? type->second.qualifiedName : r.typeName;
            out << "    pmem_ptr<persistent<" << name << ">> " << r.field << ";\n";
        }
        out << "};\n\n"
            << "__pers_root* __pers_get_root();\n";
        return out.str();
    }

    // The definition goes in the main file only. A function-local static, not
    // a global, so another file's global initializers can call it safely.
    std::string buildAccessor() {
        return "__pers_root* __pers_get_root() {\n"
               "    static __pers_root* root = pmem_root<__pers_root>();\n"
               "    return root;\n"
               "}\n\n";
    }

    // Applies `edits` to the file's current contents. Insertions at the same
    // offset keep the order they were added in.
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
}

bool emitRoots(const RootTable& table) {
    std::vector<std::string> errors;
    std::vector<MergedRoot> roots = table.merge(errors);

    if (table.mainFiles.size() != 1) {
        std::string where;
        for (const auto& [f, offset] : table.mainFiles) where += " " + f;
        errors.push_back(table.mainFiles.empty()
            ? "no main() in the given files; __pers_get_root() is defined next to main()"
            : "main() is defined in more than one file:" + where);
    }

    std::map<std::string, std::string> fieldFor;   // root key -> field
    for (const MergedRoot& r : roots) fieldFor[r.key] = r.field;

    std::map<std::string, std::vector<TextEdit>> edits;   // file -> its edits
    for (const auto& [loc, site] : table.initSites) {
        auto field = fieldFor.find(site.rootKey);
        if (field == fieldFor.end()) {
            errors.push_back("init site at " + site.location + " assigns '" + site.varName
                             + "', which is not a root of the given files");
            continue;
        }
        std::string text = "pmem_get_or_create<persistent<" + site.typeName
                         + ">>(__pers_get_root()->" + field->second;
        if (!site.initArgs.empty()) text += ", " + site.initArgs;
        text += ")";
        edits[site.file].push_back({site.offset, site.length, text});
    }

    if (!errors.empty()) {
        for (const auto& e : errors) llvm::errs() << "error: " << e << "\n";
        return false;
    }

    const auto& [mainFile, mainOffset] = *table.mainFiles.begin();
    llvm::SmallString<256> headerPath(llvm::sys::path::parent_path(mainFile));
    llvm::sys::path::append(headerPath, llvm::sys::path::stem(mainFile) + "_pers_root.hpp");
    std::string header = headerPath.str().str();

    edits[mainFile].push_back({mainOffset, 0, buildAccessor()});
    for (auto& [file, fileEdits] : edits)
        fileEdits.insert(fileEdits.begin(),
                         {0, 0, "#include \"" + includeSpelling(file, header) + "\"\n"});

    // Compute every new file before writing any, so a failure leaves the
    // sources as they were.
    std::map<std::string, std::string> output;
    output[header] = buildHeader(table, roots, header);
    for (auto& [file, fileEdits] : edits)
        if (!applyEdits(file, fileEdits, output[file], errors)) break;
    if (!errors.empty()) {
        for (const auto& e : errors) llvm::errs() << "error: " << e << "\n";
        return false;
    }

    bool ok = true;
    for (const auto& [file, text] : output) ok &= writeFile(file, text);
    return ok;
}
