#include "rootSetup.h"
#include "../utils/utils.h"
#include <algorithm>
#include <tuple>
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
            case RootLinkage::StaticLocal: return "static-local";
        }
        return "?";
    }

    std::map<std::string, std::vector<const RootDeclRecord*>>
    groupByKey(const std::map<std::string, RootDeclRecord>& decls) {
        std::map<std::string, std::vector<const RootDeclRecord*>> merged;
        for (const auto& [loc, d] : decls)
            merged[d.key].push_back(&d);
        return merged;
    }

    // The root's field in __pers_root: its key as an identifier, `::` -> `__`
    // and any other character that cannot appear in a name -> `_`:
    //   x -> x,  a::x -> a__x,  (anonymous namespace)::x -> anon__x,
    //   main()::n -> main__n,  f(int)::cache -> f_int___cache
    std::string fieldName(const RootDeclRecord& d) {
        std::string key = d.key, out;
        auto replaceAll = [&](const std::string& from, const std::string& to) {
            for (size_t at; (at = key.find(from)) != std::string::npos;) key.replace(at, from.size(), to);
        };
        replaceAll("(anonymous namespace)", "anon");
        replaceAll("()", "");
        for (size_t i = 0; i < key.size(); i++) {
            if (key.compare(i, 2, "::") == 0) { out += "__"; i++; continue; }
            out += isalnum(static_cast<unsigned char>(key[i])) ? key[i] : '_';
        }
        return out;
    }

    // The declaration as the user wrote it, for messages and --dump-roots.
    std::string declSpelling(const RootDeclRecord& d) {
        std::string slot = "persistent<persistent<" + d.typeName + ">*>";
        switch (d.shape) {
            case RootShape::Value:   return "persistent<" + d.typeName + ">";
            case RootShape::Scalar:  return slot;
            case RootShape::Fixed:   return slot + " [" + (d.arraySize ? std::to_string(d.arraySize) : "") + "]";
            case RootShape::Dynamic: return "persistent<" + slot + "*>";
        }
        return slot;
    }

    // The root's field in __pers_root.
    std::string fieldDecl(RootShape shape, unsigned size, const std::string& type,
                          const std::string& field) {
        std::string slot = "pmem_ptr<persistent<" + type + ">>";
        switch (shape) {
            case RootShape::Value:
            case RootShape::Scalar:  return slot + " " + field + ";";
            case RootShape::Fixed:   return slot + " " + field + "[" + std::to_string(size) + "];";
            case RootShape::Dynamic: return "pmem_ptr<" + slot + "> " + field + ";";
        }
        return "";
    }
}

std::vector<MergedRoot> RootTable::merge(std::vector<std::string>& errors) const {
    std::vector<MergedRoot> roots;
    std::map<std::string, std::string> fieldOwner;   // field -> key that took it

    for (const auto& [key, group] : groupByKey(decls)) {
        const RootDeclRecord& first = *group.front();
        // Two file-private variables (`static`, or in an anonymous namespace)
        // with the same qualified name in different files are different
        // variables that one key cannot tell apart; nothing else about them
        // is worth reporting.
        auto other = std::find_if(group.begin(), group.end(), [&](const RootDeclRecord* d) {
            return d->linkage != RootLinkage::External && d->file != first.file;
        });
        if (other != group.end()) {
            errors.push_back("roots named '" + key + "' in " + first.location + " and "
                             + (*other)->location + " are different variables with the same "
                             "name; rename one");
            continue;
        }
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

        static const char* kinds[] = {"init site", "array init site"};
        for (const auto& [loc, s] : initSites) {
            if (s.rootKey != key) continue;
            out << "  " << kinds[static_cast<int>(s.kind)]
                << (s.atFileScope ? " (in __pers_init)" : "") << " at " << s.location
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
        if (vd->getDeclContext()->isFunctionOrMethod()) return RootLinkage::StaticLocal;
        if (vd->hasExternalFormalLinkage()) return RootLinkage::External;
        return RootLinkage::Internal;
    }

    using utils::extractTargetFromPersistentType;

    // True if `t` is a persistent<...> object, or an array of them: the only
    // types a root can have.
    bool isPersistentObject(clang::QualType t, const clang::ASTContext& ctx) {
        return !extractTargetFromPersistentType(ctx.getBaseElementType(t)).isNull();
    }

    // For a pmem slot persistent<X*>, the X it points at; otherwise null.
    clang::QualType slotTarget(clang::QualType t) {
        clang::QualType x = extractTargetFromPersistentType(t);
        if (x.isNull() || !x->isPointerType()) return {};
        return x->getPointeeType().getCanonicalType();
    }

    struct RootInfo {
        RootShape shape = RootShape::Scalar;
        unsigned arraySize = 0;     // Fixed: N, or 0 for an extern `r[]`
        clang::QualType element;    // the T whose objects the root holds
    };

    // What a root of type `qt` holds, or nothing if root-setup does not
    // support that type as a root:
    //   persistent<persistent<T>*>               -> {Scalar,  element T}
    //   persistent<persistent<T>*>[N]            -> {Fixed,   N, element T}
    //   persistent<persistent<T>*>[]  (extern)   -> {Fixed,   0, element T}
    //   persistent<persistent<persistent<T>*>*>  -> {Dynamic, element T}
    //   persistent<T>, e.g. persistent<List>      -> {Value,   element T}
    //   persistent<T>[N]                         -> nothing (by-value arrays)
    // T itself is never a pointer, so a slot of slots of slots is nothing too.
    std::optional<RootInfo> buildRootInfo(clang::QualType qt, const clang::ASTContext& ctx) {
        RootInfo root;
        clang::QualType t = qt.getCanonicalType();
        if (const clang::ArrayType* at = ctx.getAsArrayType(t)) {
            root.shape = RootShape::Fixed;
            if (const auto* cat = llvm::dyn_cast<clang::ConstantArrayType>(at))
                root.arraySize = cat->getSize().getZExtValue();
            else if (!llvm::isa<clang::IncompleteArrayType>(at))
                return std::nullopt;
            t = at->getElementType().getCanonicalType();
        }
        clang::QualType object = slotTarget(t);         // persistent<T>, or another slot
        if (object.isNull()) {
            root.element = extractTargetFromPersistentType(t);
            if (root.element.isNull() || root.shape == RootShape::Fixed) return std::nullopt;
            root.shape = RootShape::Value;
            return root;
        }
        if (clang::QualType inner = slotTarget(object); !inner.isNull()) {
            if (root.shape == RootShape::Fixed) return std::nullopt;
            root.shape = RootShape::Dynamic;
            object = inner;
        }
        root.element = extractTargetFromPersistentType(object);
        if (root.element.isNull() || root.element->isPointerType()) return std::nullopt;
        return root;
    }

    // A root starts out null in a new pool, so `= nullptr`, `= 0` and `= {}`
    // need no init site.
    bool isNullInit(const clang::Expr* e, clang::ASTContext& ctx) {
        e = e->IgnoreUnlessSpelledInSource();
        if (llvm::isa<clang::ImplicitValueInitExpr>(e)) return true;
        // `persistent<persistent<T>*> r;` and `r[N];`: the slot's default constructor.
        if (const auto* ce = llvm::dyn_cast<clang::CXXConstructExpr>(e)) return ce->getNumArgs() == 0;
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
            if (fd->isMain()) {
                table.mainFiles[fileOf(SM, begin)] = SM.getFileOffset(begin);
                if (const auto* body = llvm::dyn_cast<clang::CompoundStmt>(fd->getBody()))
                    table.mainBodies[fileOf(SM, begin)] =
                        SM.getFileOffset(SM.getFileLoc(body->getLBracLoc())) + 1;
            }
            if (fd->getDeclName().isIdentifier() && fd->getName() == "__pers_get_root")
                table.accessorFiles.insert(fileOf(SM, begin));
            return true;
        }

        bool VisitVarDecl(clang::VarDecl* vd) {
            // Only a persistent<...> object, or an array of them, can be a root.
            if (!isPersistentObject(vd->getType(), context)) return true;
            clang::SourceLocation loc = SM.getFileLoc(vd->getLocation());
            if (SM.isInSystemHeader(loc)) return true;
            std::string name = vd->getNameAsString();
            if (!utils::isRootCandidate(vd)) {
                if (vd->isLocalVarDecl())
                    error(loc, "'" + name + "' is a persistent object with automatic storage; "
                               "add 'static' to make it a root, or declare it at file scope");
                return true;
            }
            auto root = buildRootInfo(vd->getType(), context);
            if (!root)
                return error(loc, "root '" + name + "' has a shape root-setup does not support "
                                  "yet; a root is persistent<T> r, persistent<persistent<T>*> r, "
                                  "persistent<persistent<T>*> r[N] or "
                                  "persistent<persistent<persistent<T>*>*> r");

            std::string typeName = root->element.getAsString(printPolicy);
            std::string where = locationOf(SM, loc);
            // A lambda's type has no name that stays the same across builds.
            if (const auto* md = llvm::dyn_cast<clang::CXXMethodDecl>(vd->getDeclContext()))
                if (vd->isStaticLocal() && md->getParent()->isLambda())
                    return error(loc, "root '" + name + "' is a static local of a lambda, which has "
                                      "no stable name; move it into a named function");

            RootDeclRecord d{typeName, name, root->shape, root->arraySize, linkageOf(vd),
                             vd->isThisDeclarationADefinition() != clang::VarDecl::DeclarationOnly,
                             fileOf(SM, loc), where, 0, 0, "", rootKeyOf(vd)};
            declarationRange(vd, d);
            recordType(typeName, root->element);
            if (d.isDefinition && d.error.empty())
                recordCreation(vd, *root, d);
            table.decls.emplace(where, d);
            return true;
        }

        // Every other mention of a root becomes its slot; see UseRecord.
        bool VisitDeclRefExpr(clang::DeclRefExpr* dre) {
            const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl());
            if (!vd || !utils::isRootCandidate(vd) || !buildRootInfo(vd->getType(), context))
                return true;
            clang::SourceLocation b = dre->getBeginLoc(), e = dre->getEndLoc();
            clang::SourceLocation fb = SM.getFileLoc(b);
            if (SM.isInSystemHeader(fb)) return true;
            std::string name = vd->getNameAsString();
            UseRecord u{rootKeyOf(vd), fileOf(SM, fb), SM.getFileOffset(fb), 0, locationOf(SM, fb), ""};
            if (b.isMacroID() || e.isMacroID())
                u.error = "root '" + name + "' is used inside a macro, which cannot be rewritten";
            else
                u.length = SM.getFileOffset(endOf(e)) - u.offset;
            table.uses.emplace(u.location, u);
            return true;
        }

    private:
        RootTable& table;
        const clang::PrintingPolicy& printPolicy;
        clang::ASTContext& context;
        const clang::SourceManager& SM;
        // Where each global root declaration starts, so two roots declared in one
        // declaration (`persistent<...> a, b;`) are caught. Keyed by file too: two
        // files can have declarations at the same offset.
        std::set<std::pair<clang::FileID, unsigned>> globalBegins;

        bool error(clang::SourceLocation loc, const std::string& what) {
            table.errors[locationOf(SM, loc)] = what;
            return true;
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
                    || !globalBegins.insert({SM.getFileID(b), SM.getFileOffset(b)}).second) {
                    d.error = own;
                    return;
                }
                e = next->getEndLoc();
            }
            d.offset = SM.getFileOffset(b);
            d.length = SM.getFileOffset(e) - d.offset;
        }

        // Records how a root's definition creates its object, if it does:
        //   persistent<persistent<T>*> r = new persistent<T>(args);   Object
        //   persistent<persistent<persistent<T>*>*> r = new X[n];       Array
        //   persistent<T> r(args); / = v; / no initializer              Object
        // A pointer root with no initializer, or `= nullptr`, starts null and
        // creates nothing. A Value root is always created.
        void recordCreation(const clang::VarDecl* vd, const RootInfo& root, RootDeclRecord& d) {
            const clang::Expr* init = vd->getInit();
            InitSiteRecord site;
            site.kind = InitKind::Object;
            site.textOffset = 0;
            if (root.shape == RootShape::Value) {
                if (init) std::tie(site.initArgs, site.textOffset) = constructorArgs(init);
            } else {
                if (!init || isNullInit(init, context)) return;
                const auto* ne = llvm::dyn_cast<clang::CXXNewExpr>(init->IgnoreUnlessSpelledInSource());
                if (!ne) {
                    d.error = "root '" + d.varName + "' can only be initialised with new";
                    return;
                }
                if (ne->isArray()) {
                    site.kind = InitKind::Array;
                    site.newText = textOf(ne->getSourceRange());
                    site.textOffset = SM.getFileOffset(SM.getFileLoc(ne->getBeginLoc()));
                } else if (clang::SourceRange r = utils::initArgsRange(ne); r.isValid()) {
                    site.initArgs = utils::extractInitArgs(ne, context);
                    site.textOffset = SM.getFileOffset(SM.getFileLoc(r.getBegin())) + 1;
                }
            }
            if (init) collectRoots(init, site.uses);
            site.typeName = d.typeName;
            site.varName = d.varName;
            site.rootKey = d.key;
            site.file = d.file;
            site.offset = d.offset;
            site.length = d.length;
            site.atFileScope = !vd->getDeclContext()->isFunctionOrMethod();
            site.location = d.location;
            table.initSites.emplace(site.location, site);
        }

        // The arguments of `persistent<T> r(args)`, `r{args}` or `r = v`, and
        // where they start in the file.
        std::pair<std::string, unsigned> constructorArgs(const clang::Expr* init) {
            const clang::Expr* e = init->IgnoreUnlessSpelledInSource();
            std::vector<const clang::Expr*> args;
            if (const auto* ce = llvm::dyn_cast<clang::CXXConstructExpr>(e)) {
                for (const clang::Expr* a : ce->arguments())
                    if (!llvm::isa<clang::CXXDefaultArgExpr>(a)) args.push_back(a);
            } else if (const auto* ile = llvm::dyn_cast<clang::InitListExpr>(e)) {
                for (const clang::Expr* a : ile->inits()) args.push_back(a);
            } else {
                args.push_back(e);
            }
            if (args.empty()) return {"", 0};
            clang::SourceLocation b = args.front()->getBeginLoc();
            return {textOf({b, args.back()->getEndLoc()}), SM.getFileOffset(SM.getFileLoc(b))};
        }

        // The key of the root `v` names, wherever it is used: its qualified
        // name, and for a static local its function's, with the parameter
        // types to tell overloads apart: f(int)::cache.
        std::string rootKeyOf(const clang::VarDecl* v) {
            const auto* fd = llvm::dyn_cast<clang::FunctionDecl>(v->getDeclContext());
            if (!v->isStaticLocal() || !fd) return v->getQualifiedNameAsString();
            std::string params;
            for (const clang::ParmVarDecl* p : fd->parameters())
                params += (params.empty() ? "" : ", ") + p->getType().getCanonicalType().getAsString(printPolicy);
            return fd->getQualifiedNameAsString() + "(" + params + ")::" + v->getNameAsString();
        }

        // Adds the key of every root `s` names to `keys`.
        void collectRoots(const clang::Stmt* s, std::set<std::string>& keys) {
            if (!s) return;
            if (const auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(s))
                if (const auto* v = llvm::dyn_cast<clang::VarDecl>(dre->getDecl()))
                    if (utils::isRootCandidate(v) && buildRootInfo(v->getType(), context))
                        keys.insert(rootKeyOf(v));
            for (const clang::Stmt* child : s->children()) collectRoots(child, keys);
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
    const clang::SourceManager& SM = context.getSourceManager();
    std::string file = utils::fileOf(SM, SM.getLocForStartOfFile(SM.getMainFileID()));
    table.tuFiles.push_back(file);
    table.tuSizes[file] = SM.getBufferData(SM.getMainFileID()).size();
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
                            const std::vector<std::string>& initFunctions,
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
        for (const auto& f : initFunctions) out << "void " << f << "();\n";
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
    std::string slotText(const std::string& field) {
        return "__pers_get_root()->" + field;
    }

    // The text any other use of a root becomes: its slot in __pers_root, or
    // the object in it for a Value root. Either has the same type as the
    // variable it replaces, so nothing else changes.
    std::string useSiteText(const std::string& field, RootShape shape) {
        return shape == RootShape::Value ? "(*" + slotText(field) + ")" : slotText(field);
    }

    // The note a root's declaration leaves behind, as option (b) removes it.
    std::string declSiteComment(const RootDeclRecord& d, const std::string& field) {
        return "/* root '" + d.varName + "' is " + useSiteText(field, d.shape) + " */";
    }

    // The expression that creates a root's object, if its slot is still null.
    // `text` is the site's initArgs or newText, with its root uses rewritten.
    std::string initSiteText(const InitSiteRecord& site, const std::string& field,
                             const std::string& text) {
        std::string slot = slotText(field);
        if (site.kind == InitKind::Array)
            // In one transaction, so a crash leaves no array behind.
            return "pmem::obj::transaction::run(pmem_pool(), [&] { if (!" + slot + ") "
                 + slot + " = " + text + "; })";
        std::string args = text.empty() ? "" : ", " + text;
        return "pmem_get_or_create<persistent<" + site.typeName + ">>(" + slot + args + ")";
    }

    // What a static local root's declaration becomes when it creates an
    // object: a static flag whose initializer is the creation. C++ runs a
    // static local's initializer once, when first reached (behind a guard that
    // also makes concurrent first calls wait), so the creation runs once per
    // run, where the user's declaration stood. Its uses are the slot, as for a
    // global.
    //   static persistent<persistent<int>*> n = new persistent<int>(start);
    //     -> [[maybe_unused]] static bool __pers_init_n =
    //            (pmem_get_or_create<persistent<int>>(__pers_get_root()->n, start), true);
    std::string staticLocalText(const std::string& field, const std::string& creation) {
        return "[[maybe_unused]] static bool __pers_init_" + field + " = (" + creation + ", true);";
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
    std::map<std::string, RootShape> shapeFor;     // root key -> shape
    for (const MergedRoot& r : roots) {
        fieldFor[r.key] = r.field;
        shapeFor[r.key] = r.shape;
    }

    std::map<std::string, std::vector<TextEdit>> edits;   // file -> its edits
    // Text a declaration replaces; uses inside it are covered.
    std::map<std::string, std::vector<std::pair<unsigned, unsigned>>> replaced;

    // The site's initArgs or newText with each use of a root inside it
    // rewritten, as the text moves away from where those uses were recorded.
    auto creationText = [&](const InitSiteRecord& site) {
        std::string text = site.kind == InitKind::Object ? site.initArgs : site.newText;
        std::vector<TextEdit> inside;
        for (const auto& [loc, u] : table.uses)
            if (u.file == site.file && u.offset >= site.textOffset
                && u.offset + u.length <= site.textOffset + text.size() && fieldFor.count(u.rootKey))
                inside.push_back({u.offset - site.textOffset, u.length,
                                  useSiteText(fieldFor[u.rootKey], shapeFor[u.rootKey])});
        std::sort(inside.begin(), inside.end(),
                  [](const TextEdit& a, const TextEdit& b) { return a.offset > b.offset; });
        for (const TextEdit& e : inside) text.replace(e.offset, e.length, e.text);
        return text;
    };

    // Global creations, by root key.
    std::map<std::string, const InitSiteRecord*> globalSites;
    std::map<std::pair<std::string, unsigned>, const InitSiteRecord*> localSites;   // (file, offset)
    for (const auto& [loc, site] : table.initSites) {
        if (!fieldFor.count(site.rootKey)) continue;  // reported by merge
        if (site.atFileScope) globalSites[site.rootKey] = &site;
        else localSites[{site.file, site.offset}] = &site;
    }

    // Option (b): the variable is gone. Its declarations leave a note behind,
    // or, for a static local that creates an object, the static flag that
    // creates it (staticLocalText).
    for (const auto& [loc, d] : table.decls) {
        auto field = fieldFor.find(d.key);
        if (field == fieldFor.end()) continue;
        auto site = localSites.find({d.file, d.offset});
        std::string text = site == localSites.end()
            ? declSiteComment(d, field->second)
            : staticLocalText(field->second,
                              initSiteText(*site->second, field->second, creationText(*site->second)));
        edits[d.file].push_back({d.offset, d.length, text});
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
        edits[u.file].push_back({u.offset, u.length, useSiteText(field->second, shapeFor[u.rootKey])});
    }

    // Global creations: each in __pers_init_<field>() at the end of its file.
    // main() calls them so that every root is created before an initializer
    // that uses it; otherwise in the order the files were given, then declared.
    auto rank = [&](const InitSiteRecord* site) {
        auto tu = std::find(table.tuFiles.begin(), table.tuFiles.end(), site->file);
        return std::make_pair(tu - table.tuFiles.begin(), site->offset);
    };
    std::vector<std::string> order;     // root keys
    std::set<std::string> created;
    while (order.size() < globalSites.size()) {
        const std::string* next = nullptr;
        for (const auto& [key, site] : globalSites) {
            if (created.count(key)) continue;
            bool ready = std::all_of(site->uses.begin(), site->uses.end(), [&](const std::string& k) {
                return !globalSites.count(k) || created.count(k);
            });
            if (ready && (!next || rank(site) < rank(globalSites.at(*next)))) next = &key;
        }
        if (!next) {
            std::string cycle;
            for (const auto& [key, site] : globalSites)
                if (!created.count(key)) cycle += " '" + site->varName + "'";
            errors.push_back("the initializers of roots" + cycle + " use each other, so none "
                             "can be created first");
            break;
        }
        created.insert(*next);
        order.push_back(*next);
    }

    std::vector<std::string> initFunctions;
    std::map<std::string, std::map<unsigned, std::string>> bodies;   // file -> offset -> function
    for (const std::string& key : order) {
        const InitSiteRecord& site = *globalSites.at(key);
        if (!table.tuSizes.count(site.file)) {
            errors.push_back(site.location + ": root '" + site.varName + "' creates its object "
                             "in a file that is not one of the given .cpp files; define it in a .cpp file");
            continue;
        }
        std::string fn = "__pers_init_" + fieldFor[key];
        bodies[site.file][site.offset] = "\nvoid " + fn + "() {\n    "
            + initSiteText(site, fieldFor[key], creationText(site)) + ";\n}\n";
        initFunctions.push_back(fn);
    }
    for (const auto& [file, functions] : bodies) {
        std::string text;
        for (const auto& [offset, fn] : functions) text += fn;
        edits[file].push_back({table.tuSizes.at(file), 0, text});
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
    std::string calls;
    for (const auto& fn : initFunctions) calls += "\n    " + fn + "();";
    if (!calls.empty()) edits[mainFile].push_back({table.mainBodies.at(mainFile), 0, calls});
    for (auto& [file, fileEdits] : edits)
        fileEdits.insert(fileEdits.begin(),
                         {0, 0, "#include \"" + includeSpelling(file, header) + "\"\n"});

    // Compute every new file before writing any, so a failure leaves the
    // sources as they were.
    std::map<std::string, std::string> output;
    output[header] = buildHeader(table, roots, initFunctions, header);
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
