#include "rootSetup.h"
#include "../utils/utils.h"
#include <llvm/Support/raw_ostream.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/ExprCXX.h>            // CXXNewExpr
#include <clang/Lex/Lexer.h>              // Lexer::getSourceText
#include <llvm/Support/FileSystem.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceManager.h>
#include <sstream>
#include <optional>
namespace {
    bool isRootCandidate(const clang::VarDecl* vd){
        if(llvm::isa<clang::ParmVarDecl>(vd)) return false;
        const clang::DeclContext* dc = vd->getDeclContext();
        if(dc->isTranslationUnit()) return true;
        if(auto* fd = llvm::dyn_cast<clang::FunctionDecl>(dc)) {
            if(fd->isMain()) return true;
        }
        return false;
    }
    std::optional<std::string> 
    tryExtractPersistentTypeName(clang::QualType qt, const clang::PrintingPolicy& policy){
        if(qt->isPointerType()) qt = qt->getPointeeType();
        const clang::CXXRecordDecl* rd = qt->getAsCXXRecordDecl();
        if(!rd) return std::nullopt;
        const auto* ctsd = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rd);
        if(!ctsd) return std::nullopt;
        if(ctsd->getSpecializedTemplate()->getNameAsString() != "persistent") return std::nullopt;
        return ctsd->getTemplateArgs()[0].getAsType().getAsString(policy);
    }

    std::string extractInitArgs(const clang::CXXNewExpr* newExpr, clang::ASTContext& context) {
        clang::SourceRange parens = newExpr->getDirectInitRange();
        if(parens.isInvalid()) return "";
        std::string text = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(parens),
            context.getSourceManager(),
            context.getLangOpts()).str();

            if(text.size()>= 2 && text.front() == '(' && text.back() == ')') {
                return text.substr(1, text.size()-2);
            }
            return "";
    }
    class PersistentDeclVisitor : public clang::RecursiveASTVisitor<PersistentDeclVisitor> {
    public:
        PersistentDeclVisitor(std::vector<PersistentDecl>& decls, 
                              std::vector<InitSite>& initSites,
                              const clang::PrintingPolicy& policy, clang::ASTContext& context)
            : decls(decls), initSites(initSites), printPolicy(policy), context(context) {}
        bool VisitVarDecl(clang::VarDecl *varDecl){
            if(!isRootCandidate(varDecl)) return true;
            auto typeName = tryExtractPersistentTypeName(varDecl->getType(), printPolicy);
            if(!typeName) return true;

            bool isPointer = varDecl->getType()->isPointerType();
            decls.push_back({*typeName,varDecl->getNameAsString(), isPointer});

            if(varDecl->hasInit()) {
                const clang::Expr* init = varDecl->getInit()->IgnoreImpCasts();
                if(const auto* newExpr = llvm::dyn_cast<clang::CXXNewExpr>(init)) {
                    initSites.push_back({
                        *typeName, varDecl->getNameAsString(),
                        newExpr->getSourceRange(),
                        extractInitArgs(newExpr, context)
                    });
                }
            }
            return true;
        }
    private:
        std::vector<PersistentDecl>& decls;
        std::vector<InitSite>& initSites;
        const clang::PrintingPolicy& printPolicy;
        clang::ASTContext& context;
    };

    class AssignmentInitVisitor : public clang::RecursiveASTVisitor<AssignmentInitVisitor>{
        public:
            AssignmentInitVisitor(std::vector<InitSite>& initSites,
                                 const clang::PrintingPolicy& policy, clang::ASTContext& context)
                : initSites(initSites), printPolicy(policy), context(context) {}

            bool VisitBinaryOperator(clang::BinaryOperator* bo) {
                if(bo->getOpcode() != clang::BO_Assign) return true;

                const clang::Expr* lhs = bo->getLHS()->IgnoreImpCasts();
                const auto* dref = llvm::dyn_cast<clang::DeclRefExpr>(lhs);
                if(!dref) return true;
                const auto * vd = llvm::dyn_cast<clang::VarDecl>(dref->getDecl());
                if(!vd) return true;
                if(!isRootCandidate(vd)) return true;

                auto typeName = tryExtractPersistentTypeName(vd->getType(), printPolicy);
                if(!typeName) return true;

                const clang::Expr* rhs = bo->getRHS()->IgnoreImpCasts();
                const auto* newExpr = llvm::dyn_cast<clang::CXXNewExpr>(rhs);
                if(!newExpr) return true;

                initSites.push_back({
                    *typeName, vd->getNameAsString(),
                    newExpr->getSourceRange(),
                    extractInitArgs(newExpr, context)
                });
                return true;
            }
        private:
            std::vector<InitSite>& initSites;
            const clang::PrintingPolicy& printPolicy;
            clang::ASTContext& context;
    };

}


RootSetup::RootSetup(clang::ASTContext &context, clang::Rewriter &rewriter)
        : context(context), rewriter(rewriter), printPolicy(context.getPrintingPolicy()) {
            printPolicy.SuppressTagKeyword = true;
        }

void RootSetup::start() {
    llvm::outs() << "Starting RootSetup transformation\n";
    findPersistentDecls();
    applyRewrites();
    writeOutput();
    // for(auto& d: persistentDecls) {
    //     std::string rangeText = clang::Lexer::getSourceText(
    //     clang::CharSourceRange::getTokenRange(d.newExprRange),
    //     context.getSourceManager(),
    //     context.getLangOpts()).str();
    //     llvm::outs() << "Found: persistent<" << d.typeName << ">"
    //                 << (d.isPointer ? "* " : " ") << d.varName
    //                 << "  init=`" << rangeText << "`"
    //                 << "  args=`" << d.initArgs << "`\n";
    // }
}

void RootSetup::findPersistentDecls() {
    PersistentDeclVisitor visitor(persistentDecls, initSites, printPolicy, context);
    visitor.TraverseDecl(context.getTranslationUnitDecl());

    AssignmentInitVisitor assignVisitor(initSites, printPolicy, context);
    assignVisitor.TraverseDecl(context.getTranslationUnitDecl());
}

std::string RootSetup::buildRootStruct() const {
    std::ostringstream out;
    out<< "struct __pers_root {\n";
    for(const auto& d: persistentDecls) {
        out<< "    " << (d.isPointer ? "pmem_ptr<" : "") 
            << "persistent<" << d.typeName << ">" 
            << (d.isPointer ? ">" : "") 
            << " " << d.varName << ";\n";
    }
    out<< "};\n\n";
    out<< "__pers_root* __root = pmem_root<__pers_root>();\n";
    return out.str();
}

std::string RootSetup::buildInitializers() const {
    std::ostringstream out;
    for(const auto& d: persistentDecls) {
        out<< "persistent< " << d.typeName << "> " 
            <<(d.isPointer ? "* " : "") << d.varName << " = "
            <<"pmem_get_or_create< persistent<" 
            << d.typeName << ">>(__root->" << d.varName << ");\n";
    }
    return out.str();
}

clang::SourceLocation RootSetup::findMainLocation() {
    auto* tu = context.getTranslationUnitDecl();
    for (auto* d : tu->decls()) {
        if (auto* fd = llvm::dyn_cast<clang::FunctionDecl>(d)) {
            if (fd->getNameAsString() == "main") {
                return fd->getBeginLoc();
            }
        }
    }
    return {};
}

void RootSetup::applyRewrites() {
    clang::SourceLocation mainLoc = findMainLocation();
    if (mainLoc.isInvalid())
    {
        llvm::errs() << "Error: Could not find main() function\n";
        return;
    }
    
    std::string preamble = buildRootStruct() + "\n";
    rewriter.InsertTextBefore(mainLoc, preamble);

    for(const auto& site : initSites) {
        std::ostringstream rep;
        rep << "pmem_get_or_create<persistent<" << site.typeName << ">>(__root->" <<  site.varName;
        
        if (!site.initArgs.empty()) rep << ", " << site.initArgs;
        rep << ")";

        rewriter.ReplaceText(
            clang::CharSourceRange::getTokenRange(site.newExprRange),
            rep.str());
    }
}

void RootSetup::writeOutput() {
    utils::writeAllRewrites(rewriter, context);
}