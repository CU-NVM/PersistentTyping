#ifndef CHECKPERSISTENTTYPES_H
#define CHECKPERSISTENTTYPES_H

#include <map>
#include <string>
#include <clang/AST/ASTContext.h>
#include "../utils/utils.h"

// check-persistent-types checks every persistent<...> the user wrote and
// rewrites nothing. The later passes read types by their canonical form, so
// persistent<persistent<Node>*> and pmem_ptr<persistent<Node>> need no
// rewriting to look the same. Errors:
//     persistent<Node*>              a pmem pointer's target must be
//                                    persistent<...> too (rule 2)
//     persistent<persistent<int>>    one layer wrapped twice
//     persistent<int[4]>             write persistent<int> x[4]
//     persistent<void*>              a pmem pointer needs a pointee type
//     persistent<void (*)(int)>      function pointers cannot live in pmem
// Everything else (references, unions, std:: types, ...) is left to the
// library's static_asserts and the later passes.
//
// Like root-setup it runs across translation units in two steps:
//   1. CheckPersistentTypesCollector visits each TU and records its errors
//   2. reportCheckPersistentTypes, run once after every TU, reports them
// so an error in a header included by several TUs is reported once.

struct CheckPersistentTypesTable {
    // location -> message, so an error in a shared header is reported once.
    std::map<std::string, std::string> errors;
};

class CheckPersistentTypesCollector {
    public:
        CheckPersistentTypesCollector(clang::ASTContext &context, CheckPersistentTypesTable &table);
        void start();
    private:
        clang::ASTContext &context;
        CheckPersistentTypesTable &table;
};

// Reports every error. Returns false if there were any.
bool reportCheckPersistentTypes(const CheckPersistentTypesTable& table);

#endif
