#ifndef MEMBERPERSISTIFIER_H
#define MEMBERPERSISTIFIER_H

#include <clang/AST/ASTContext.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace clang {
    class CXXMethodDecl;
    class FieldDecl;
    class Stmt;
}

// Called with each class the persistified code uses as persistent<C>, so C
// gets a specialization too (`new Helper` in a method reaches Helper).
using QueueSpecializationFn = std::function<void(const std::string&)>;

// Persistifies the code inside a persistent class: method and constructor
// bodies, constructor initializer lists and default member initializers.
// SpecializationBuilder decides the class's shape (fields, signatures) and
// asks this class for everything that comes after a signature or field name.
// The work is done by rewriters (memberPersistifier.cc) that edit a scratch
// copy of the original class's code, which stays well-typed.
class MemberPersistifier {
public:
    // `policy` is held by reference: the builder finishes setting it up after
    // this object is constructed. `queueSpecialization` is told every class
    // the persistified code uses as persistent<C>.
    MemberPersistifier(clang::ASTContext& context, const clang::PrintingPolicy& policy,
                       QueueSpecializationFn queueSpecialization);

    // The text after `method`'s signature: " : inits {...}" for a constructor
    // with a written initializer list, " {...}" otherwise, wrapped in a
    // transaction unless the method only reads. `method` must have a body.
    std::string persistifyDefinition(const clang::CXXMethodDecl* method);

    // " = x", "{x}" or "" for `field`'s default member initializer.
    std::string persistifyDefaultMemberInit(const clang::FieldDecl* field);

private:
    clang::ASTContext& context;
    const clang::PrintingPolicy& printPolicy;
    QueueSpecializationFn queueSpecialization;
    std::map<const clang::CXXMethodDecl*, bool> readOnly;     // by canonical decl
    std::set<const clang::CXXMethodDecl*> checkingReadOnly;   // in progress: recursion

    // Runs the rewriters (new T -> new persistent<T>, etc.) over `s`,
    // recording their edits in `scratch`.
    void runRewriters(clang::Stmt* s, clang::Rewriter& scratch);
    // " : inits {...}" for a constructor with a written initializer list.
    std::string persistifyConstructorDefinition(const clang::CXXConstructorDecl* ctor);
    // Wraps a rewritten method body ("{...}") in a PMDK transaction.
    std::string wrapInTransaction(const std::string& body, const clang::CXXMethodDecl* method);
    // True if `method` cannot write persistent data, so it needs no
    // transaction (closure doc 8.2, the read-only exemption). Judged on the
    // body as written; anything it cannot prove harmless counts as a write.
    bool isReadOnlyMethod(const clang::CXXMethodDecl* method);
};

#endif
