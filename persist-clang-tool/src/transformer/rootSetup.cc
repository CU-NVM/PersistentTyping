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

    // The declaration as the user wrote it, for messages and --dump-roots.
    std::string declSpelling(const RootDeclRecord& d) {
        std::string t = "persistent<" + d.typeName + ">";
        if (!d.isPointer) return t;
        switch (d.shape) {
            case RootShape::Scalar:  return t + "*";
            case RootShape::Fixed:   return t + "* [" + (d.arraySize ? std::to_string(d.arraySize) : "") + "]";
            case RootShape::Dynamic: return t + "**";
        }
        return t;
    }

    // The root's field in __pers_root.
    std::string fieldDecl(RootShape shape, unsigned size, const std::string& type,
                          const std::string& field) {
        std::string slot = "pmem_ptr<persistent<" + type + ">>";
        switch (shape) {
            case RootShape::Scalar:  return slot + " " + field + ";";
            case RootShape::Fixed:   return slot + " " + field + "[" + std::to_string(size) + "];";
            case RootShape::Dynamic: return "pmem_ptr<" + slot + "> " + field + ";";
        }
        return "";
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
        const RootDeclRecord* def = &first;
        unsigned defs = 0;
        for (const RootDeclRecord* d : group)
            if (d->isDefinition && !defs++) def = d;
        for (const RootDeclRecord* d : group) {
            if (!d->error.empty())
                errors.push_back(d->location + ": " + d->error);
            if (d->typeName != first.typeName)
                errors.push_back("root '" + key + "' is persistent<" + first.typeName + "> at "
                                 + first.location + " but persistent<" + d->typeName
                                 + "> at " + d->location);
            else if (!d->isPointer)
                errors.push_back("root '" + key + "' at " + d->location
                                 + " is not a pointer; only persistent<T>* roots are supported,"
                                   " and root arrays persistent<T>* r[N] and persistent<T>** r");
            else if (d->shape != def->shape
                     || (d->arraySize && def->arraySize && d->arraySize != def->arraySize))
                errors.push_back("root '" + key + "' is " + declSpelling(*def) + " at "
                                 + def->location + " but " + declSpelling(*d) + " at " + d->location);
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
        roots.push_back({key, field, first.typeName, def->shape, def->arraySize});
    }
    return roots;
}

void RootTable::print(llvm::raw_ostream& out) const {
    auto merged = groupByKey(decls);
    out << "== roots: " << merged.size() << " ==\n";
    for (const auto& [key, group] : merged) {
        const RootDeclRecord& first = *group.front();
        out << "\n" << key << "  [" << linkageName(first.linkage) << "]\n"
            << "  field: " << fieldDecl(first.shape, first.arraySize, first.typeName, fieldName(first))
            << "\n";

        for (const RootDeclRecord* d : group)
            out << "  " << (d->isDefinition ? "definition " : "declaration")
                << " " << declSpelling(*d) << "  at " << d->location << "\n";

        static const char* kinds[] = {"init site", "array init site", "element init site"};
        for (const auto& [loc, s] : initSites) {
            if (s.rootKey != key) continue;
            out << "  " << kinds[static_cast<int>(s.kind)] << " at " << s.location
                << "  (offset " << s.offset << ", length " << s.length << ")"
                << (s.initArgs.empty() ? "" : "  args=`" + s.initArgs + "`") << "\n";
        }
        unsigned n = 0;
        for (const auto& [loc, u] : uses) n += u.rootKey == key;
        if (n) out << "  uses: " << n << "\n";
    }

    out << "\nmain() in:";
    for (const auto& [f, offset] : mainFiles) out << " " << f;
    if (mainFiles.empty()) out << " (none)";
    out << "\n";
}

// ---- RootCollector: recording one TU into the table ----

namespace {
    using utils::fileOf;
    using utils::locationOf;

    RootLinkage linkageOf(const clang::VarDecl* vd) {
        if (vd->getDeclContext()->isFunctionOrMethod()) return RootLinkage::MainLocal;
        if (vd->hasExternalFormalLinkage()) return RootLinkage::External;
        return RootLinkage::Internal;
    }

    // The library's persistent<X>, or null.
    const clang::ClassTemplateSpecializationDecl* asPersistent(clang::QualType t) {
        const auto* ctsd = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
            t->getAsCXXRecordDecl());
        if (!ctsd || ctsd->getSpecializedTemplate()->getName() != "persistent") return nullptr;
        if (!ctsd->getSpecializedTemplate()->getDeclContext()->getRedeclContext()->isTranslationUnit())
            return nullptr;
        return ctsd;
    }

    struct RootType {
        RootShape shape = RootShape::Scalar;
        unsigned arraySize = 0;
        bool isPointer = true;
        clang::QualType element;    // the T of persistent<T>
    };

    // The shape of a root variable's type: persistent<T>*, persistent<T>* [N]
    // or persistent<T>**, or persistent<T> by value (reported as an error).
    // `persistent<persistent<T>*>* r` (= pmem_ptr<persistent<T>>* r) is a
    // pointer to a slot, which is the dynamic array persistent<T>** too.
    std::optional<RootType> rootTypeOf(clang::QualType qt, const clang::ASTContext& ctx) {
        RootType rt;
        clang::QualType t = qt.getCanonicalType();
        if (const clang::ArrayType* at = ctx.getAsArrayType(t)) {
            rt.shape = RootShape::Fixed;
            if (const auto* cat = llvm::dyn_cast<clang::ConstantArrayType>(at))
                rt.arraySize = cat->getSize().getZExtValue();
            else if (!llvm::isa<clang::IncompleteArrayType>(at))
                return std::nullopt;
            t = at->getElementType().getCanonicalType();
        }
        if (!t->isPointerType()) {
            const auto* spec = rt.shape == RootShape::Scalar ? asPersistent(t) : nullptr;
            if (!spec) return std::nullopt;
            rt.isPointer = false;
            rt.element = spec->getTemplateArgs()[0].getAsType();
            return rt;
        }
        clang::QualType pointee = t->getPointeeType().getCanonicalType();
        if (rt.shape == RootShape::Scalar && pointee->isPointerType()) {
            rt.shape = RootShape::Dynamic;
            pointee = pointee->getPointeeType().getCanonicalType();
        } else if (rt.shape == RootShape::Scalar) {
            if (const auto* slot = asPersistent(pointee)) {
                clang::QualType arg = slot->getTemplateArgs()[0].getAsType().getCanonicalType();
                if (arg->isPointerType()) {
                    rt.shape = RootShape::Dynamic;
                    pointee = arg->getPointeeType().getCanonicalType();
                }
            }
        }
        const auto* spec = asPersistent(pointee);
        if (!spec) return std::nullopt;
        rt.element = spec->getTemplateArgs()[0].getAsType();
        if (rt.element.getCanonicalType()->isPointerType()) return std::nullopt;
        return rt;
    }

    // True if `t` names the library's persistent<> anywhere: through
    // pointers, references, arrays or template arguments.
    bool mentionsPersistent(clang::QualType t, unsigned depth = 0) {
        if (depth > 16) return false;
        t = t.getCanonicalType();
        if (t->isPointerType() || t->isReferenceType())
            return mentionsPersistent(t->getPointeeType(), depth + 1);
        if (t->isArrayType())
            return mentionsPersistent(t->getAsArrayTypeUnsafe()->getElementType(), depth + 1);
        if (asPersistent(t)) return true;
        if (const auto* ctsd = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(
                t->getAsCXXRecordDecl()))
            for (const clang::TemplateArgument& arg : ctsd->getTemplateArgs().asArray())
                if (arg.getKind() == clang::TemplateArgument::Type
                    && mentionsPersistent(arg.getAsType(), depth + 1))
                    return true;
        return false;
    }

    // A root starts out null in a new pool, so `= nullptr`, `= 0` and `= {}`
    // need no init site.
    bool isNullInit(const clang::Expr* e, clang::ASTContext& ctx) {
        e = e->IgnoreParenImpCasts();
        if (llvm::isa<clang::ImplicitValueInitExpr>(e)) return true;
        if (const auto* ile = llvm::dyn_cast<clang::InitListExpr>(e)) return ile->getNumInits() == 0;
        return e->isNullPointerConstant(ctx, clang::Expr::NPC_ValueDependentIsNotNull)
               != clang::Expr::NPCK_NotNull;
    }

    class RootCollectVisitor : public clang::RecursiveASTVisitor<RootCollectVisitor> {
    public:
        RootCollectVisitor(RootTable& table, const clang::PrintingPolicy& policy,
                           clang::ASTContext& context)
            : table(table), printPolicy(policy), context(context),
              SM(context.getSourceManager()) {}

        bool VisitFunctionDecl(clang::FunctionDecl* fd) {
            if (!fd->doesThisDeclarationHaveABody()) return true;
            clang::SourceLocation begin = SM.getFileLoc(fd->getBeginLoc());
            if (fd->isMain())
                table.mainFiles[fileOf(SM, begin)] = SM.getFileOffset(begin);
            if (fd->getDeclName().isIdentifier() && fd->getName() == "__pers_get_root")
                table.accessorFiles.insert(fileOf(SM, begin));
            return true;
        }

        bool VisitVarDecl(clang::VarDecl* vd) {
            auto rt = rootOf(vd);
            if (!rt) {
                // Something persistent the program keeps in a variable that
                // root-setup cannot make a slot of: say so instead of leaving
                // it in DRAM.
                if (isUserCandidate(vd) && mentionsPersistent(vd->getType())) {
                    clang::SourceLocation loc = SM.getFileLoc(vd->getLocation());
                    if (!SM.isInSystemHeader(loc))
                        error(loc, "'" + vd->getNameAsString() + "' mentions persistent<> but is "
                                   "not a root: a root is persistent<T>* r, persistent<T>* r[N] "
                                   "or persistent<T>** r");
                }
                return true;
            }
            clang::SourceLocation loc = SM.getFileLoc(vd->getLocation());
            if (SM.isInSystemHeader(loc)) return true;

            std::string typeName = rt->element.getAsString(printPolicy);
            std::string where = locationOf(SM, loc);
            RootDeclRecord d{typeName, vd->getNameAsString(), rt->shape, rt->arraySize,
                             rt->isPointer, linkageOf(vd),
                             vd->isThisDeclarationADefinition() != clang::VarDecl::DeclarationOnly,
                             fileOf(SM, loc), where, 0, 0, ""};
            declarationRange(vd, d);
            recordType(typeName, rt->element);
            if (vd->hasInit() && d.error.empty() && rt->isPointer)
                recordDeclInit(vd, *rt, d);
            table.decls.emplace(where, d);
            return true;
        }

        // `r = new persistent<T>(...)`, `r = new persistent<T>*[n]` and
        // `r[i] = new persistent<T>(...)` outside the declaration.
        bool VisitBinaryOperator(clang::BinaryOperator* bo) {
            if (bo->getOpcode() != clang::BO_Assign) return true;
            const auto* ne = llvm::dyn_cast<clang::CXXNewExpr>(bo->getRHS()->IgnoreParenImpCasts());
            if (!ne) return true;
            const clang::Expr* lhs = bo->getLHS()->IgnoreParenImpCasts();
            const clang::Expr* index = nullptr;
            if (const auto* ase = llvm::dyn_cast<clang::ArraySubscriptExpr>(lhs)) {
                index = ase->getIdx();
                lhs = ase->getBase()->IgnoreParenImpCasts();
            }
            const auto* dref = llvm::dyn_cast<clang::DeclRefExpr>(lhs);
            const auto* vd = dref ? llvm::dyn_cast<clang::VarDecl>(dref->getDecl()) : nullptr;
            auto rt = rootOf(vd);
            if (!rt || !rt->isPointer) return true;
            clang::SourceLocation begin = SM.getFileLoc(bo->getBeginLoc());
            if (SM.isInSystemHeader(begin)) return true;

            InitSiteRecord site = siteFor(vd, *rt, ne, bo->getSourceRange());
            if (index) {
                if (rt->shape == RootShape::Scalar || ne->isArray())
                    return error(begin, "'" + vd->getNameAsString() + "[i] = new ...' needs a root array "
                                        "and a single object");
                site.kind = InitKind::Element;
                site.index = textOf(index->getSourceRange());
            } else if (rt->shape == RootShape::Fixed) {
                return error(begin, "root array '" + vd->getNameAsString() + "' is initialised "
                                    "element by element: " + vd->getNameAsString()
                                    + "[i] = new persistent<T>(...)");
            } else if ((rt->shape == RootShape::Dynamic) != ne->isArray()) {
                return error(begin, rt->shape == RootShape::Dynamic
                    ? "dynamic root array '" + vd->getNameAsString() + "' is allocated with "
                      + vd->getNameAsString() + " = new persistent<T>*[n]"
                    : "root '" + vd->getNameAsString() + "' holds one object, not an array");
            } else if (rt->shape == RootShape::Dynamic && !isStatement(bo)) {
                return error(begin, "write '" + vd->getNameAsString() + " = new ...[n];' as a "
                                    "statement of its own");
            }
            table.initSites.emplace(site.location, site);
            return true;
        }

        // Every other mention of a root becomes its slot; see UseRecord.
        bool VisitDeclRefExpr(clang::DeclRefExpr* dre) {
            const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl());
            auto rt = rootOf(vd);
            if (!rt || !rt->isPointer) return true;
            clang::SourceLocation b = dre->getBeginLoc(), e = dre->getEndLoc();
            clang::SourceLocation fb = SM.getFileLoc(b);
            if (SM.isInSystemHeader(fb)) return true;
            std::string name = vd->getNameAsString();
            std::string key = RootTable::mergeKey(linkageOf(vd),
                                                  fileOf(SM, SM.getFileLoc(vd->getLocation())), name);
            UseRecord u{key, fileOf(SM, fb), SM.getFileOffset(fb), 0, false, 0,
                        locationOf(SM, fb), ""};
            if (b.isMacroID() || e.isMacroID()) {
                u.error = "root '" + name + "' is used inside a macro, which cannot be rewritten";
                table.uses.emplace(u.location, u);
                return true;
            }
            u.length = SM.getFileOffset(endOf(e)) - u.offset;

            const clang::Stmt* top = dre;
            bool read = climb(top);
            if (rt->shape == RootShape::Scalar) {
                u.read = read;
            } else {
                const auto* ase = parentOf(top).get<clang::ArraySubscriptExpr>();
                if (!ase || ase->getBase()->IgnoreParenImpCasts() != dre) {
                    u.error = "root array '" + name + "' can only be used as " + name + "[i]";
                } else {
                    const clang::Stmt* elem = ase;
                    if (climb(elem))
                        u.elementEnd = SM.getFileOffset(endOf(ase->getEndLoc()));
                }
            }
            table.uses.emplace(u.location, u);
            return true;
        }

    private:
        RootTable& table;
        const clang::PrintingPolicy& printPolicy;
        clang::ASTContext& context;
        const clang::SourceManager& SM;
        std::set<unsigned> globalBegins;    // where each global root declaration starts

        // A root candidate the user wrote: not a helper root-setup itself
        // emitted on an earlier run (`__pers_init_<field>`).
        bool isUserCandidate(const clang::VarDecl* vd) {
            if (!vd || !utils::isRootCandidate(vd)) return false;
            return !(vd->getDeclName().isIdentifier() && vd->getName().starts_with("__pers_"));
        }

        std::optional<RootType> rootOf(const clang::VarDecl* vd) {
            if (!isUserCandidate(vd)) return std::nullopt;
            return rootTypeOf(vd->getType(), context);
        }

        bool error(clang::SourceLocation loc, const std::string& what) {
            table.errors[locationOf(SM, loc)] = what;
            return true;
        }

        clang::DynTypedNode parentOf(const clang::Stmt* s) {
            auto parents = context.getParents(*s);
            return parents.empty() ? clang::DynTypedNode() : parents[0];
        }

        // Walks up from `s` through parentheses and implicit casts, leaving
        // `s` at the outermost one. True if a load (lvalue-to-rvalue
        // conversion) was among them: the value is read, not written.
        bool climb(const clang::Stmt*& s) {
            bool read = false;
            for (;;) {
                clang::DynTypedNode p = parentOf(s);
                if (const auto* ice = p.get<clang::ImplicitCastExpr>()) {
                    read |= ice->getCastKind() == clang::CK_LValueToRValue;
                    s = ice;
                } else if (const auto* pe = p.get<clang::ParenExpr>()) {
                    s = pe;
                } else {
                    return read;
                }
            }
        }

        bool isStatement(const clang::Expr* e) {
            clang::DynTypedNode p = parentOf(e);
            return p.get<clang::Stmt>() && !p.get<clang::Expr>();
        }

        clang::SourceLocation endOf(clang::SourceLocation tokenLoc) {
            return clang::Lexer::getLocForEndOfToken(SM.getFileLoc(tokenLoc), 0, SM,
                                                     context.getLangOpts());
        }

        std::string textOf(clang::SourceRange r) {
            return clang::Lexer::getSourceText(
                clang::CharSourceRange::getTokenRange(SM.getFileLoc(r.getBegin()),
                                                      SM.getFileLoc(r.getEnd())),
                SM, context.getLangOpts()).str();
        }

        InitSiteRecord siteFor(const clang::VarDecl* vd, const RootType& rt,
                               const clang::CXXNewExpr* ne, clang::SourceRange range) {
            clang::SourceLocation begin = SM.getFileLoc(range.getBegin());
            unsigned offset = SM.getFileOffset(begin);
            InitSiteRecord site;
            site.kind = ne->isArray() ? InitKind::Array : InitKind::Object;
            site.typeName = rt.element.getAsString(printPolicy);
            site.varName = vd->getNameAsString();
            site.rootKey = RootTable::mergeKey(linkageOf(vd),
                                               fileOf(SM, SM.getFileLoc(vd->getLocation())),
                                               site.varName);
            site.file = fileOf(SM, begin);
            site.offset = offset;
            site.length = SM.getFileOffset(endOf(range.getEnd())) - offset;
            site.initArgs = utils::extractInitArgs(ne, context);
            if (ne->isArray() && ne->getArraySize())
                site.count = textOf((*ne->getArraySize())->getSourceRange());
            site.inDeclaration = false;
            site.atFileScope = false;
            site.location = locationOf(SM, begin);
            return site;
        }

        // The declaration's text with its `;`: a local's DeclStmt, or a
        // global from its first token to the `;` after it. Option (b)
        // replaces this text, so it must hold this one declarator only.
        void declarationRange(const clang::VarDecl* vd, RootDeclRecord& d) {
            std::string own = "declare root '" + d.varName + "' in a declaration of its own";
            clang::SourceLocation b, e;
            if (vd->getDeclContext()->isFunctionOrMethod()) {
                auto parents = context.getParents(*vd);
                const auto* ds = parents.empty() ? nullptr : parents[0].get<clang::DeclStmt>();
                if (!ds || !ds->isSingleDecl()) { d.error = own; return; }
                b = SM.getFileLoc(ds->getBeginLoc());
                e = endOf(ds->getEndLoc());
            } else {
                b = SM.getFileLoc(vd->getSourceRange().getBegin());
                auto next = clang::Lexer::findNextToken(SM.getFileLoc(vd->getSourceRange().getEnd()),
                                                        SM, context.getLangOpts());
                if (!next || !next->is(clang::tok::semi)
                    || !globalBegins.insert(SM.getFileOffset(b)).second) {
                    d.error = own;
                    return;
                }
                e = next->getEndLoc();
            }
            d.offset = SM.getFileOffset(b);
            d.length = SM.getFileOffset(e) - d.offset;
        }

        // `persistent<T>* r = new persistent<T>(...);` and the like: the init
        // site replaces the whole declaration.
        void recordDeclInit(const clang::VarDecl* vd, const RootType& rt, RootDeclRecord& d) {
            const clang::Expr* init = vd->getInit();
            const auto* ne = llvm::dyn_cast<clang::CXXNewExpr>(init->IgnoreParenImpCasts());
            if (!ne) {
                if (!isNullInit(init, context))
                    d.error = "root '" + d.varName + "' can only be initialised with "
                              "new persistent<T>(...)";
                return;
            }
            if (rt.shape == RootShape::Fixed) {
                d.error = "root array '" + d.varName + "' is initialised element by element: "
                          + d.varName + "[i] = new persistent<T>(...)";
                return;
            }
            if ((rt.shape == RootShape::Dynamic) != ne->isArray()) {
                d.error = rt.shape == RootShape::Dynamic
                    ? "dynamic root array '" + d.varName + "' is allocated with new persistent<T>*[n]"
                    : "root '" + d.varName + "' holds one object, not an array";
                return;
            }
            InitSiteRecord site = siteFor(vd, rt, ne, vd->getSourceRange());
            site.offset = d.offset;
            site.length = d.length;
            site.inDeclaration = true;
            site.atFileScope = !vd->getDeclContext()->isFunctionOrMethod();
            site.location = d.location;
            table.initSites.emplace(site.location, site);
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
    using utils::TextEdit;

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
            out << "    " << fieldDecl(r.shape, r.arraySize, name, r.field) << "\n";
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

    using utils::applyEdits;
    using utils::writeFile;
}

namespace {
    // The text an init site becomes. `slot` is __pers_get_root()->field.
    std::string initSiteText(const InitSiteRecord& site, const std::string& field) {
        std::string elem = "persistent<" + site.typeName + ">";
        std::string slot = "__pers_get_root()->" + field;
        std::string args = site.initArgs.empty() ? "" : ", " + site.initArgs;
        if (site.kind == InitKind::Array) {
            // Allocate only if the slot is still null, so a later run reuses
            // the array; in one transaction, so a crash leaves no array behind.
            std::string body = "if (!" + slot + ") " + slot + " = new pmem_ptr<" + elem + ">["
                             + site.count + "]; ";
            if (site.atFileScope)
                return "[[maybe_unused]] static const bool __pers_init_" + field
                     + " = (pmem::obj::transaction::run(pmem_pool(), [] { " + body + "}), true);";
            std::string tx = "pmem::obj::transaction::run(pmem_pool(), [&] { " + body + "})";
            return site.inDeclaration ? tx + ";" : tx;
        }
        if (site.kind == InitKind::Element) slot += "[" + site.index + "]";
        std::string call = "pmem_get_or_create<" + elem + ">(" + slot + args + ")";
        if (!site.inDeclaration) return call;
        // A global initialiser still has to run before main(), so it stays a
        // declaration, of a helper the program never names.
        if (site.atFileScope)
            return "[[maybe_unused]] static " + elem + "* const __pers_init_" + field + " = "
                 + call + ";";
        return call + ";";
    }

    bool within(unsigned offset, const std::vector<std::pair<unsigned, unsigned>>& ranges) {
        for (const auto& [b, e] : ranges)
            if (b <= offset && offset < e) return true;
        return false;
    }
}

bool emitRoots(const RootTable& table) {
    std::vector<std::string> errors;
    for (const auto& [where, what] : table.errors) errors.push_back(where + ": " + what);
    std::vector<MergedRoot> roots = table.merge(errors);

    if (table.mainFiles.size() != 1) {
        std::string where;
        for (const auto& [f, offset] : table.mainFiles) where += " " + f;
        errors.push_back(table.mainFiles.empty()
            ? "no main() in the given files; __pers_get_root() is defined next to main()"
            : "main() is defined in more than one file:" + where);
    }

    // root-setup's own output: the roots are already slots in __pers_root
    // and __pers_get_root() is defined, so there is nothing left to do.
    if (!table.accessorFiles.empty()) {
        if (roots.empty() && errors.empty()) {
            llvm::outs() << "root-setup: already applied to these files; nothing to do\n";
            return true;
        }
        errors.push_back("__pers_get_root() is already defined in " + *table.accessorFiles.begin()
                         + ": root-setup has already run on these files; add roots to the "
                           "original sources and run it there");
    }

    std::map<std::string, std::string> fieldFor;   // root key -> field
    for (const MergedRoot& r : roots) fieldFor[r.key] = r.field;

    std::map<std::string, std::vector<TextEdit>> edits;   // file -> its edits
    // Text an init site or declaration replaces; uses inside it are covered.
    std::map<std::string, std::vector<std::pair<unsigned, unsigned>>> replaced;
    std::set<std::pair<std::string, unsigned>> declsWithInit;

    for (const auto& [loc, site] : table.initSites) {
        auto field = fieldFor.find(site.rootKey);
        if (field == fieldFor.end()) {
            errors.push_back("init site at " + site.location + " assigns '" + site.varName
                             + "', which is not a root of the given files");
            continue;
        }
        edits[site.file].push_back({site.offset, site.length, initSiteText(site, field->second)});
        replaced[site.file].push_back({site.offset, site.offset + site.length});
        if (site.inDeclaration) declsWithInit.insert({site.file, site.offset});
    }

    // Option (b): the variable is gone; its declarations leave a note behind.
    for (const auto& [loc, d] : table.decls) {
        auto field = fieldFor.find(RootTable::mergeKey(d));
        if (field == fieldFor.end() || declsWithInit.count({d.file, d.offset})) continue;
        edits[d.file].push_back({d.offset, d.length,
                                 "/* root '" + d.varName + "' is __pers_get_root()->"
                                 + field->second + " */"});
        replaced[d.file].push_back({d.offset, d.offset + d.length});
    }

    for (const auto& [loc, u] : table.uses) {
        if (within(u.offset, replaced[u.file])) continue;
        if (!u.error.empty()) {
            errors.push_back(u.location + ": " + u.error);
            continue;
        }
        auto field = fieldFor.find(u.rootKey);
        if (field == fieldFor.end()) continue;  // a declaration-only root: reported by merge
        std::string slot = "__pers_get_root()->" + field->second;
        edits[u.file].push_back({u.offset, u.length, u.read ? slot + ".get()" : slot});
        if (u.elementEnd) edits[u.file].push_back({u.elementEnd, 0, ".get()"});
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
