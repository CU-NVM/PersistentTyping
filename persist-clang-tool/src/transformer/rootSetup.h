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
//
// A root variable *is* its slot in __pers_root (option b): its declarations
// go away, and every use of it, in every file, becomes __pers_get_root()->r.

// How a root variable is visible across translation units. Only External
// roots can be named from another TU; the other two stay in their own file.
// MainLocal covers a local of main() and a static local of any function.
enum class RootLinkage { External, Internal, MainLocal };

// What a root declaration holds, and the __pers_root field it becomes:
//   Scalar   persistent<T>* r          -> pmem_ptr<persistent<T>> r;
//   Fixed    persistent<T>* r[N]       -> pmem_ptr<persistent<T>> r[N];
//   Dynamic  persistent<T>** r         -> pmem_ptr<pmem_ptr<persistent<T>>> r;
// Keeping N, or the n of `r = new persistent<T>*[n]`, the same across runs is
// the program's responsibility, as with any other C++ array.
enum class RootShape { Scalar, Fixed, Dynamic };

// One declaration of a root variable. A root usually has one definition and
// any number of `extern` declarations in other files or headers.
struct RootDeclRecord {
    std::string typeName;
    std::string varName;
    RootShape shape;
    unsigned arraySize;     // Fixed: N, or 0 for `extern persistent<T>* r[];`
    bool isPointer;         // false for `persistent<T> r;`, which is an error
    RootLinkage linkage;
    bool isDefinition;
    std::string file;
    std::string location;   // file:line:col
    // The whole declaration, `;` included, which is replaced: by nothing (a
    // comment), or by the init site when it initialises the root.
    unsigned offset;
    unsigned length;
    std::string error;      // a problem only this declaration has
};

// A statement that initialises a root, recorded by file offset because a
// clang::SourceRange is only valid inside its own TU.
//   Object   r = new persistent<T>(args)     -> pmem_get_or_create(slot, args)
//   Array    r = new persistent<T>*[n]       -> allocate only if the slot is null
//   Element  r[i] = new persistent<T>(args)  -> pmem_get_or_create(slot[i], args)
enum class InitKind { Object, Array, Element };

struct InitSiteRecord {
    InitKind kind;
    std::string typeName;
    std::string varName;
    std::string rootKey;    // RootTable::mergeKey of the root it initializes
    std::string file;
    unsigned offset;        // the whole assignment, or the whole declaration
    unsigned length;
    std::string initArgs;   // Object, Element
    std::string index;      // Element: the text of i
    std::string count;      // Array: the text of n
    bool inDeclaration;     // `persistent<T>* r = new ...;`
    bool atFileScope;       // ... of a global, so it must stay a declaration
    std::string location;
};

// Any other mention of a root, which becomes its slot:
//   read             r       -> __pers_get_root()->r.get()    (same type as before)
//   written, &r      r       -> __pers_get_root()->r
//   element read     r[i]    -> __pers_get_root()->r[i].get()
//   element written  r[i]    -> __pers_get_root()->r[i]
struct UseRecord {
    std::string rootKey;
    std::string file;
    unsigned offset;
    unsigned length;
    bool read;              // append .get() to the slot
    unsigned elementEnd;    // element read: offset just past `]`, else 0
    std::string location;
    std::string error;
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
    RootShape shape;
    unsigned arraySize;
};

// Everything RootCollector finds, across every TU of the program. Records are
// keyed by location so a header included by several TUs counts once.
struct RootTable {
    std::map<std::string, RootDeclRecord> decls;
    std::map<std::string, InitSiteRecord> initSites;
    std::map<std::string, UseRecord> uses;
    std::map<std::string, RootTypeInfo> types;    // by type name
    std::map<std::string, unsigned> mainFiles;    // file -> offset where main() begins
    std::set<std::string> accessorFiles;          // files already defining __pers_get_root
    std::map<std::string, std::string> errors;    // location -> problem found while collecting

    // The key a declaration merges under: its name for External roots, and
    // file::name for roots that are private to one file.
    static std::string mergeKey(const RootDeclRecord& d);
    static std::string mergeKey(RootLinkage linkage, const std::string& file,
                                const std::string& varName);

    // One entry per root, ordered by key. Every conflict between declarations
    // (type or shape mismatch, missing or repeated definition, two roots
    // wanting the same field) is appended to `errors`.
    std::vector<MergedRoot> merge(std::vector<std::string>& errors) const;

    // Prints one entry per merged root, the field it would get in
    // __pers_root, and every declaration, init site and use behind it.
    void print(llvm::raw_ostream& out) const;
};

// First half of root-setup across translation units: records every root
// declaration, init site, use and main() in one TU into the shared RootTable.
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
//   - every root declaration is removed, or replaced by its init site
//   - every init site and use becomes the root's slot, as described above
//   - every file it rewrites includes the header
//   - __pers_get_root() is defined once, just before main()
// Files that root-setup has already rewritten (main() already has
// __pers_get_root next to it, and no roots are left) are left alone.
// Returns false, after reporting each problem, without writing anything.
bool emitRoots(const RootTable& table);

#endif
