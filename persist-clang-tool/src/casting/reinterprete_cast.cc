
// #include "RecursiveNumaTyper.h"
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <clang/Rewrite/Core/Rewriter.h>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include <sstream>
#include <string>
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "reinterprete_cast.h"



void numafyAndReinterpreteCast (clang::Rewriter &rewriter, clang::SourceManager &SM,const clang::LangOptions &LO, clang::CXXNewExpr* CXXNewExpr, llvm::APSInt nodeID){
    std::string NewType = CXXNewExpr->getAllocatedType().getAsString();
    std::string CastType = CXXNewExpr->getType().getAsString();
    if(CXXNewExpr->isArray()){
        // --- extract the array size expression text from the AST (no hardcoding) ---
        std::string sizeText;
        if (std::optional<clang::Expr *> Sz = CXXNewExpr->getArraySize()) {
            clang::CharSourceRange szRange =
                clang::CharSourceRange::getTokenRange((*Sz)->getSourceRange());
            sizeText = clang::Lexer::getSourceText(szRange, SM, LO).str();
        }
        if (sizeText.empty()) sizeText = "1"; // fallback if somehow missing

        // --- build the full replacement for the *new* expression itself ---
        // CastType is your target, e.g., "HashNode **"
        // NewType is the element type string, e.g., "HashNode *"
        // nodeID is the template int param
        std::string newExprReplacement =
            "reinterpret_cast<" + CastType + ">("
            "reinterpret_cast<" + CastType + ">("
                "new numa<" + NewType + "," + std::to_string(nodeID.getExtValue()) + ">["
                + sizeText + "]))";

        // --- precisely replace the entire CXXNewExpr with our rebuilt text ---
        clang::SourceLocation newBeg   = SM.getSpellingLoc(CXXNewExpr->getBeginLoc());
        clang::SourceLocation newEndTk = SM.getSpellingLoc(CXXNewExpr->getEndLoc());
        clang::SourceLocation newAfter = clang::Lexer::getLocForEndOfToken(newEndTk, 0, SM, LO);
        clang::CharSourceRange newRange = clang::CharSourceRange::getCharRange(newBeg, newAfter);

        // (Optional) sanity logs
        // llvm::outs() << "Old new-expr: [" << rewriter.getRewrittenText({newBeg, newEndTk}) << "]\n";
        // llvm::outs() << "New new-expr: [" << newExprReplacement << "]\n";

        rewriter.ReplaceText(newRange, newExprReplacement);
    }
    else{
        // --- extract constructor initializer text (paren/brace) from the AST ---
        std::string initText;    // e.g. "42, x+y"   or   "1, 2"   or   "" (no args)
        bool useParens = true;   // switch to braces if list-init

        if (const clang::Expr *Init = CXXNewExpr->getInitializer()) {
            const clang::Expr *I = Init->IgnoreImplicit();

            if (const auto *CCE = llvm::dyn_cast<clang::CXXConstructExpr>(I)) {
                useParens = !CCE->isListInitialization();
                // Collect each arg's exact source text
                std::string pieces;
                for (unsigned i = 0; i < CCE->getNumArgs(); ++i) {
                    const clang::Expr *A = CCE->getArg(i);
                    auto rng = clang::CharSourceRange::getTokenRange(A->getSourceRange());
                    std::string one = clang::Lexer::getSourceText(rng, SM, LO).str();
                    if (!pieces.empty()) pieces += ", ";
                    pieces += one;
                }
                initText = pieces; // empty if no args
            }
            else if (const auto *ILE = llvm::dyn_cast<clang::InitListExpr>(I)) {
                useParens = false; // brace-init
                std::string pieces;
                for (unsigned i = 0; i < ILE->getNumInits(); ++i) {
                    const clang::Expr *E = ILE->getInit(i);
                    auto rng = clang::CharSourceRange::getTokenRange(E->getSourceRange());
                    std::string one = clang::Lexer::getSourceText(rng, SM, LO).str();
                    if (!pieces.empty()) pieces += ", ";
                    pieces += one;
                }
                initText = pieces; // could be empty "{}"
            }
            else {
                // Fallback: take the whole initializer tokens as-is
                auto rng = clang::CharSourceRange::getTokenRange(I->getSourceRange());
                initText = clang::Lexer::getSourceText(rng, SM, LO).str();
                // Heuristically assume parens if the text doesn't already include braces
                useParens = (initText.find('{') == std::string::npos);
                // If the text already includes surrounding () or {}, leave as-is below.
            }
        }

        // --- build "new numa<NewType,NODE>(...)" or "{...}" or no args ---
        std::string ctorSuffix;
        if (initText.empty()) {
            // no initializer at all → emit nothing (just "new numa<...>")
            ctorSuffix = "";
        } else {
            // If initText already includes leading '(' or '{', keep it verbatim.
            char c0 = initText.front();
            if (c0 == '(' || c0 == '{') {
                ctorSuffix = initText;  // already wrapped
            } else {
                ctorSuffix = useParens ? ("(" + initText + ")") : ("{" + initText + "}");
            }
        }

        std::string newExprReplacement =
            "reinterpret_cast<" + CastType + ">("
            "reinterpret_cast<" + CastType + ">("
                "new numa<" + NewType + "," + std::to_string(nodeID.getExtValue()) + ">"
                + ctorSuffix +
            "))";

        // --- precisely replace the entire CXXNewExpr with our rebuilt text ---
        clang::SourceLocation newBeg   = SM.getSpellingLoc(CXXNewExpr->getBeginLoc());
        clang::SourceLocation newEndTk = SM.getSpellingLoc(CXXNewExpr->getEndLoc());
        clang::SourceLocation newAfter = clang::Lexer::getLocForEndOfToken(newEndTk, 0, SM, LO);
        clang::CharSourceRange newRange = clang::CharSourceRange::getCharRange(newBeg, newAfter);

        // (Optional) sanity logs
        // llvm::outs() << "New non-array new-expr: [" << newExprReplacement << "]\n";

        rewriter.ReplaceText(newRange, newExprReplacement);
    }
}