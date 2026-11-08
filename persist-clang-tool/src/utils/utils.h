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

// Builds a method signature string for the persistent<T> spec.
// Constructors are renamed to `persistent`, destructors to `~persistent`.
// Parameter and return types go through signatureType().
// Preserves static, const, parameter names. Does NOT preserve `explicit`,
// `noexcept`, `constexpr`, attributes, default args, ref-qualifiers.
std::string
buildMethodSignature(const clang::CXXMethodDecl* method,
                     const clang::PrintingPolicy& policy);

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
std::string extractInitArgs(const clang::CXXNewExpr* newExpr,
                            clang::ASTContext& context);

// The class definition whose className(), qualified name or plain name is `name`.
const clang::CXXRecordDecl* findCXXRecordDeclByName(clang::ASTContext& context, const std::string& name,
                                                    const clang::PrintingPolicy& policy);

// How a class or enum is spelled in generated code, and the name the
// specialisation worklist uses for it: scoped, without `struct`/`class`, and
// without scopes the source never writes (anonymous and inline namespaces).
std::string className(const clang::TagDecl* td, const clang::PrintingPolicy& policy);

// P(T): the type that lives in pmem in place of T.
//   P(F)    = persistent<F>       F arithmetic or enum
//   P(C)    = persistent<C>       C a user class
//   P(U*)   = pmem_ptr<P(U)>      any U, so P(C**) = pmem_ptr<pmem_ptr<persistent<C>>>
//   P(T[N]) = P(T)[N]
//   P(const T)      = const P(T)
//   P(persistent<X>) = P(X)       so P(P(T)) = P(T); pmem_ptr<X> is persistent<X*>
// Typedefs and aliases are looked through. Everything else is an error:
// references, unions, void*, function pointers, library (std:: or system
// header) types, classes with virtual methods or base classes, and class
// templates other than persistent.
struct TypeTranslation {
    // A declaration `T name` becomes `prefix name suffix`; suffix holds the
    // array bounds, e.g. P(int[4]) is {"persistent<int>", "[4]"}.
    std::string prefix;
    std::string suffix;
    // Every user class T reaches, which each need a persistent<C>.
    std::vector<std::string> classes;
    // Why T has no persistent form; empty on success.
    std::string error;

    bool ok() const { return error.empty(); }
    std::string spelling() const { return prefix + suffix; }
    std::string declare(const std::string& name) const { return prefix + " " + name + suffix; }
};

// `aliasPointers` false spells P(U*) as persistent<P(U)*> instead of
// pmem_ptr<P(U)>, the same type, as users write it in messages.
TypeTranslation translateType(clang::QualType qt, const clang::PrintingPolicy& policy,
                              bool aliasPointers = true);

// L(T): a local, parameter or return value lives in DRAM and stays a raw
// pointer or reference; only what it points at moves to pmem.
//   L(U*) = P(U)*     L(U&) = P(U)&     L(U&&) = P(U)&&
// so L(HashNode*) = persistent<HashNode>* and
// L(HashNode**) = pmem_ptr<persistent<HashNode>>*. Error if T is not a
// pointer or reference, or P(U) has none.
TypeTranslation translateDramRef(clang::QualType qt, const clang::PrintingPolicy& policy);

// How a parameter or return type is spelled in persistent<C>: L(T) when T
// points at or refers to a user class, since every such object is in pmem;
// otherwise T as written. A pointer to a fundamental (`int* src`) is kept,
// because callers may pass DRAM.
std::string signatureType(clang::QualType qt, const clang::PrintingPolicy& policy);

// The declaration of `field` as it appears in persistent<C>: P(T) name, with
// any array bounds after the name. Appends the classes P(T) reaches to
// `reached`. If T has no persistent form, reports an error and returns the
// field as written.
std::string
wrapFieldType(const clang::FieldDecl* field,
              const clang::PrintingPolicy& policy,
              std::vector<std::string>& reached);

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
