#ifndef RECURSEPERSISTENTTYPER_H
#define RECURSEPERSISTENTTYPER_H

#include <clang/AST/ASTContext.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <map>
#include <string>
#include <set>
#include <vector>

namespace clang {
    class CXXRecordDecl;
    class CXXMethodDecl;
    class CXXConstructorDecl;
    class Stmt;
}

class RecursePersistentTyper {
public:
    struct Spec {
            const clang::CXXRecordDecl* rd;
            std::string text;
    };
    RecursePersistentTyper(clang::ASTContext &context, clang::Rewriter &rewriter);
    void start();
    void specializeType(const std::string& typeName);
    const std::map<std::string, Spec>& getSpecs() const;
private:
    clang::ASTContext &context;
    clang::Rewriter &rewriter;
    clang::PrintingPolicy printPolicy;
    std::set<std::string> existingSpecs;
    std::set<std::string> seen;
    std::vector<std::string> workList;
    std::map<std::string, Spec> specs;
    void discoverTypesFromRoot();
    std::string processClass(const std::string& name, const clang::CXXRecordDecl* decl);
    std::string processMethod(const clang::CXXMethodDecl* method);
    std::string processConstructor(const clang::CXXConstructorDecl* ctor);
    std::string processInClassInit(const clang::FieldDecl* field);
    // Runs the body visitors (new T -> new persistent<T>, etc.) over `s`,
    // recording their edits in `scratch`.
    void runRewriters(clang::Stmt* s, clang::Rewriter& scratch);
    // Wraps a rewritten method body ("{...}") in a PMDK transaction.
    std::string wrapInTransaction(const std::string& body, const clang::CXXMethodDecl* method);
};

#endif
