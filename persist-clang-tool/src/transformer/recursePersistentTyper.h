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
};

#endif
