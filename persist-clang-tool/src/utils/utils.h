#ifndef UTILS_HPP
#define UTILS_HPP

#include <fstream>
#include <vector>
#include <string>

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>

#include <clang/Tooling/Tooling.h>
#include <clang/Frontend/FrontendActions.h>
#include "clang/Tooling/CompilationDatabase.h"
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/ASTMatchers/ASTMatchers.h>
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
#include <set>
#include <map>
#include <optional>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>

using namespace clang;
namespace utils {

// True if the file backing `fid` contains `#include "header"` or `#include <header>`.
// Text-based check — doesn't see through macros or `#if 0`. Fine for our purposes.
bool fileIncludes(clang::SourceManager& SM,
                  clang::FileID fid,
                  const std::string& header);

// Returns the verbatim source text of a method body (including the {...}).
// Empty string if the method has no body.
std::string
getMethodBodyText(const clang::CXXMethodDecl* method,
                  clang::ASTContext& context);

std::set<std::string> findExistingPersistentSpecs(clang::ASTContext& context,
                                                  const clang::PrintingPolicy& policy);

// True if `vd` can be a root: a variable with static storage duration that
// is not a class member, i.e. one at namespace scope (global, in a namespace,
// in an anonymous namespace) or a static local of any function, main
// included. Automatic locals, parameters, thread_locals and static data
// members never are.
bool isRootCandidate(const clang::VarDecl* vd);

// The library's persistent<X> that `t` names, or null. Works on the canonical
// type, so spaces, typedefs, `using` aliases, alias templates and pmem_ptr<U>
// (which is persistent<U*>) all give the same answer.
const clang::ClassTemplateSpecializationDecl* persistentSpecialization(clang::QualType t);

// The X of the library's persistent<X>, as a canonical type, or a null type
// if `t` is not one:
//   persistent<int>                -> int
//   persistent<persistent<Node>*>  -> persistent<Node>*
//   Node*, int                     -> null
clang::QualType extractTargetFromPersistentType(clang::QualType t);

// The constructor arguments of `new T(args)` or `new T{args}` as source text,
// without the parentheses or braces. Empty for `new T` and `new T()`.
// The `(args)` or `{args}` of `new T(args)` / `new T{args}`, brackets
// included, or an invalid range if there is none.
clang::SourceRange initArgsRange(const clang::CXXNewExpr* newExpr);

std::string extractInitArgs(const clang::CXXNewExpr* newExpr,
                            clang::ASTContext& context);

// The class definition whose className(), qualified name or plain name is `name`.
const clang::CXXRecordDecl* findCXXRecordDeclByName(clang::ASTContext& context, const std::string& name,
                                                    const clang::PrintingPolicy& policy);

// ---- Rewriting files across translation units ----
// A clang::SourceLocation is only valid inside its own TU, so passes that
// collect edits from several TUs (root-setup) record them by real
// file path and byte offset, then apply them once after every TU is parsed.

// Real path of the file `loc` sits in, so a header reached through two
// different -I paths still gets one name.
std::string fileOf(const clang::SourceManager& SM, clang::SourceLocation loc);

// file:line:col of `loc`, with the file as fileOf gives it.
std::string locationOf(const clang::SourceManager& SM, clang::SourceLocation loc);

// Text that replaces [offset, offset + length) of a file; length 0 inserts.
struct TextEdit {
    unsigned offset;
    unsigned length;
    std::string text;
};

// Applies `edits` to the file's current contents, appending to `result`.
// Insertions at the same offset keep the order they were added in.
bool applyEdits(const std::string& file, std::vector<TextEdit> edits,
                std::string& result, std::vector<std::string>& errors);

bool writeFile(const std::string& file, const std::string& text);

// The line the tool puts first in every file it finishes transforming. It
// turns on the library's checks for transformed code (an unspecialised
// persistent<C>, a raw pointee in persistent<U*>).
extern const char* const TRANSFORMED_DEFINE;

// True if `text` already starts a file the tool has marked.
bool hasTransformedDefine(llvm::StringRef text);

// Prints `error: <what>` and counts it. A pass that reported any error exits
// non-zero (see main.cc), so a build stops instead of compiling half-
// transformed code.
void reportError(const std::string& what);
unsigned errorCount();

// Flush every modified file in the Rewriter back to disk. Iterates the
// rewriter's per-FileID buffers (not just the main file) so passes that
// rewrite headers (e.g. the recursive specializer adding `persistent<T>`
// into Stack.hpp) land on disk along with the main translation unit.
void writeAllRewrites(clang::Rewriter& rewriter, clang::ASTContext& context);

}

#endif
