#ifndef SPECIALIZATIONBUILDER_H
#define SPECIALIZATIONBUILDER_H

#include "memberPersistifier.h"
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

// The recurse-persistent pass: finds every class reachable from __pers_root
// and generates its `template<> class persistent<C>`. This class decides the
// shape (field types, signatures, operator new/delete); MemberPersistifier
// translates the code that goes inside it.
class SpecializationBuilder {
public:
    struct Spec {
            const clang::CXXRecordDecl* rd;
            std::string text;
    };
    SpecializationBuilder(clang::ASTContext &context, clang::Rewriter &rewriter);
    void run();
    void queueSpecialization(const std::string& typeName);
    const std::map<std::string, Spec>& getSpecs() const;
private:
    clang::ASTContext &context;
    clang::Rewriter &rewriter;
    clang::PrintingPolicy printPolicy;
    // Declared after printPolicy, which it holds by reference: members are
    // initialized in declaration order.
    MemberPersistifier memberPersistifier;
    std::set<std::string> existingSpecs;
    std::set<std::string> seen;
    std::vector<std::string> workList;
    std::map<std::string, Spec> specs;
    void discoverTypesFromRoot();
    clang::SourceLocation specInsertLoc(const clang::CXXRecordDecl* rd);
    void markTransformed(clang::FileID fid, std::set<unsigned>& marked);
    std::string buildSpecialization(const std::string& name, const clang::CXXRecordDecl* decl);
    // A method or constructor of the spec: its signature, then its definition
    // from MemberPersistifier (or `;`, `= default`, `= delete`).
    std::string buildMethod(const clang::CXXMethodDecl* method);
    void queueSignatureClasses(const clang::CXXMethodDecl* method);
};

#endif
