#include "rootCollector.h"
#include "../../utils/utils.h"
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

void RootCollector::run() {
    const clang::SourceManager& SM = context.getSourceManager();
    std::string file = utils::fileOf(SM, SM.getLocForStartOfFile(SM.getMainFileID()));
    table.tuFiles.push_back(file);
    table.tuSizes[file] = SM.getBufferData(SM.getMainFileID()).size();
    RootCollectVisitor visitor(table, printPolicy, context);
    visitor.TraverseDecl(context.getTranslationUnitDecl());
}
