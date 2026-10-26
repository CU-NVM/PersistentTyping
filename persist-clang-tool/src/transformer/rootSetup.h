#ifndef ROOTSETUP_H
#define ROOTSETUP_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include <clang/AST/ASTContext.h>
#include <clang/Basic/SourceLocation.h>
#include <llvm/Support/raw_ostream.h>

// root-setup across translation units runs in two steps:
//   1. RootCollector visits each TU and records its roots into one RootTable
//   2. emitRoots, run once after every TU, writes the shared root header and
//      rewrites the sources

// How a root variable is visible across translation units. Only External
// roots can be named from another TU; the other two stay in their own file.
enum class RootLinkage { External, Internal, MainLocal };

// One declaration of a root variable. A root usually has one definition and
// any number of `extern` declarations in other files or headers.
struct RootDeclRecord {
    std::string typeName;
    std::string varName;
    bool isPointer;
    RootLinkage linkage;
    bool isDefinition;
    std::string file;
    std::string location;   // file:line:col
};

// A `new persistent<T>(...)` that initializes a root, recorded by file offset
// because a clang::SourceRange is only valid inside its own TU.
struct InitSiteRecord {
    std::string typeName;
    std::string varName;
    std::string rootKey;    // RootTable::mergeKey of the root it initializes
    std::string file;
    unsigned offset;
    unsigned length;
    std::string initArgs;
    std::string location;
};

// What the shared root header needs so it can name persistent<T>. A T
// defined in a .cpp is forward-declared; one the header cannot declare
// (a template or enum) is reached by including the header that defines it.
struct RootTypeInfo {
    std::string qualifiedName;  // T spelled so it resolves at global scope
    std::string forwardDecl;    // e.g. "namespace ns { struct Box; }"
    std::string include;        // real path of the defining header
    std::string error;          // why the header cannot name T at all
};

// The root as it ends up in __pers_root, after merging its declarations.
struct MergedRoot {
    std::string key;
    std::string field;
    std::string typeName;
};

// Everything RootCollector finds, across every TU of the program. Records are
// keyed by location so a header included by several TUs counts once.
struct RootTable {
    std::map<std::string, RootDeclRecord> decls;
    std::map<std::string, InitSiteRecord> initSites;
    std::map<std::string, RootTypeInfo> types;    // by type name
    std::map<std::string, unsigned> mainFiles;    // file -> offset where main() begins

    // The key a declaration merges under: its name for External roots, and
    // file::name for roots that are private to one file.
    static std::string mergeKey(const RootDeclRecord& d);
    static std::string mergeKey(RootLinkage linkage, const std::string& file,
                                const std::string& varName);

    // One entry per root, ordered by key. Every conflict between declarations
    // (type mismatch, missing or repeated definition, two roots wanting the
    // same field) is appended to `errors`.
    std::vector<MergedRoot> merge(std::vector<std::string>& errors) const;

    // Prints one entry per merged root, the field it would get in
    // __pers_root, and every declaration and init site behind it.
    void print(llvm::raw_ostream& out) const;
};

// First half of root-setup across translation units: records every root
// declaration, init site and main() in one TU into the shared RootTable.
// Rewrites nothing; emitting happens once all TUs have been collected.
class RootCollector {
    public:
        RootCollector(clang::ASTContext &context, RootTable &table);
        void start();
    private:
        clang::ASTContext &context;
        RootTable &table;
        clang::PrintingPolicy printPolicy;
};

// Second half of root-setup, run once after every TU has been collected.
// Writes <main>_pers_root.hpp next to the file with main(): the __pers_root
// struct and the declaration of its accessor. Then rewrites the sources:
//   - every init site `new persistent<T>(args)` becomes
//     `pmem_get_or_create<persistent<T>>(__pers_get_root()->field, args)`
//   - every file with an init site, and the main file, includes the header
//   - __pers_get_root() is defined once, just before main()
// Returns false, after reporting each problem, without writing anything.
bool emitRoots(const RootTable& table);

#endif
