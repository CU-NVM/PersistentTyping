#include "checkPersistentTypes.h"
#include "../../utils/typeTranslation.h"
#include <algorithm>
#include <vector>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/raw_ostream.h>

namespace {
    using utils::extractTargetFromPersistentType;
    using utils::persistentSpecialization;

    // Why the persistent<...> type `t` is not allowed, or "" if it is. Walks
    // every pmem layer, so persistent<persistent<Node*>*> is caught at the
    // inner Node*.
    std::string checkPersistentType(clang::QualType t, const clang::PrintingPolicy& policy) {
        clang::QualType target = extractTargetFromPersistentType(t);
        std::string name = "'" + t.getAsString(policy) + "'";

        if (target->isArrayType())
            return name + ": write an array of persistent<T> elements, "
                          "`persistent<T> name[N]`, not persistent<T[N]>";

        // Each persistent<> marks one layer, so wrapping a layer twice is a mistake.
        if (!extractTargetFromPersistentType(target).isNull())
            return name + " wraps one layer twice; write " + target.getAsString(policy);

        if (!target->isPointerType()) return "";
        clang::QualType pointee = target->getPointeeType().getUnqualifiedType();
        // The library's static_asserts say the same, but only where the type
        // is completed, as with rule 2 below.
        if (pointee->isVoidType())
            return name + ": a pmem pointer must have a pointee type, not void";
        if (pointee->isFunctionType())
            return name + ": function pointers cannot live in pmem";
        // Rule 2: a pmem pointer's target must be persistent<...> too. The
        // library's static_assert catches this only where the type is
        // completed (an object, new, sizeof), never in `persistent<Node*>* p`.
        if (extractTargetFromPersistentType(pointee).isNull()) {
            // The fix as the user writes it, persistent<persistent<C>*>.
            utils::TypeTranslation fix = utils::translateType(t, policy, false);
            return name + " is a pmem pointer to a DRAM target; a pmem pointer's target "
                          "must be persistent<...> too"
                   + (fix.ok() && fix.suffix.empty() ? ", write " + fix.prefix : "");
        }
        return checkPersistentType(pointee, policy);
    }

    // A written persistent<...> or pmem_ptr<...>: a TypeLoc, which is a
    // type as it appears in the source, with its location.
    struct Written {
        clang::TemplateSpecializationTypeLoc tl;
        clang::FileID file;
        unsigned begin, end;    // [begin, end) in `file`, `>` included
    };

    // Finds every written template whose canonical type is persistent<...>
    // in one TU: persistent<...> itself, pmem_ptr<...>, and the user's alias
    // templates. Clang creates a TypeLoc wherever a type is spelled
    // (declarations, `new`, casts, sizeof, template arguments, signatures),
    // so visiting TypeLocs covers all of them.
    class CheckPersistentTypesVisitor : public clang::RecursiveASTVisitor<CheckPersistentTypesVisitor> {
    public:
        explicit CheckPersistentTypesVisitor(const clang::SourceManager& SM) : SM(SM) {}

        bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc tl) {
            const auto* ctsd = persistentSpecialization(tl.getType());
            if (!ctsd) return true;
            clang::SourceLocation b = SM.getFileLoc(tl.getBeginLoc());
            clang::SourceLocation e = SM.getFileLoc(tl.getRAngleLoc());
            if (SM.isInSystemHeader(b)) return true;
            // The library's own uses of persistent<> are not user code.
            clang::SourceLocation lib = ctsd->getSpecializedTemplate()->getLocation();
            if (SM.getFileID(b) == SM.getFileID(SM.getFileLoc(lib))) return true;
            found.push_back({tl, SM.getFileID(b), SM.getFileOffset(b), SM.getFileOffset(e) + 1});
            return true;
        }

        // Only the outermost written wrapper is checked: checkPersistentType
        // already walks the inner ones, so checking them again would report
        // one mistake twice.
        std::vector<Written> outermost() const {
            std::vector<Written> out;
            for (const Written& w : found) {
                bool inside = std::any_of(found.begin(), found.end(), [&](const Written& o) {
                    return &o != &w && o.file == w.file && o.begin <= w.begin && w.end <= o.end
                        && (o.begin != w.begin || o.end != w.end);
                });
                if (!inside) out.push_back(w);
            }
            return out;
        }

    private:
        const clang::SourceManager& SM;
        std::vector<Written> found;
    };
}

CheckPersistentTypesCollector::CheckPersistentTypesCollector(clang::ASTContext &context,
                                                             CheckPersistentTypesTable &table)
        : context(context), table(table) {}

void CheckPersistentTypesCollector::run() {
    const clang::SourceManager& SM = context.getSourceManager();
    CheckPersistentTypesVisitor visitor(SM);
    visitor.TraverseDecl(context.getTranslationUnitDecl());

    clang::PrintingPolicy policy = context.getPrintingPolicy();
    for (const Written& w : visitor.outermost()) {
        clang::QualType t = w.tl.getType();
        // persistent<T> inside a template: T is unknown until instantiation.
        if (t->isDependentType()) continue;
        std::string why = checkPersistentType(t, policy);
        if (!why.empty())
            table.errors[utils::locationOf(SM, SM.getFileLoc(w.tl.getBeginLoc()))] = why;
    }
}

bool reportCheckPersistentTypes(const CheckPersistentTypesTable& table) {
    for (const auto& [where, what] : table.errors)
        llvm::errs() << "error: " << where << ": " << what << "\n";
    return table.errors.empty();
}
