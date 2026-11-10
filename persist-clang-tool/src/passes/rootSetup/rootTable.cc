#include "rootTable.h"
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
}

std::string rootFieldDeclaration(RootShape shape, unsigned size, const std::string& type,
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
            << "  field: " << rootFieldDeclaration(first.shape, first.arraySize, first.typeName, fieldName(first))
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
