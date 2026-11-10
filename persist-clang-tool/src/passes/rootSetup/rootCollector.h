#ifndef ROOTCOLLECTOR_H
#define ROOTCOLLECTOR_H

#include "rootTable.h"
#include <clang/AST/ASTContext.h>

// First half of root-setup across translation units: records every root
// declaration, init site, use and main() in one TU into the shared RootTable.
// Rewrites nothing; emitting happens once all TUs have been collected.
class RootCollector {
    public:
        RootCollector(clang::ASTContext &context, RootTable &table);
        void run();
    private:
        clang::ASTContext &context;
        RootTable &table;
        clang::PrintingPolicy printPolicy;
};

#endif
