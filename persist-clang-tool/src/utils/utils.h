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

// True if `qt` is `T*` where T is a user-defined class/struct.
// Used to detect pointer-to-user-class members, locals, parameters, and
// return types — all of which become persistent / pmem_ptr in the spec.
bool isUserClassPointerType(clang::QualType qt);

// True if `qt` is `char*`, `const char*`, or `std::string`. These map to
// pmem_string rather than to the generic pointer/class wrappings.
bool isStringLikeType(clang::QualType qt);

// True if `qt` is already a persistence-aware library type — pmem_string,
// pmem_ptr<..>, or persistent<..>. Such fields are emitted unchanged.
bool isPersistenceAwareType(clang::QualType qt);




const clang::CXXRecordDecl* findCXXRecordDeclByName(clang::ASTContext& context, const std::string& name);

// Unwrap a type that is `persistent<T>` or `pmem_ptr<persistent<T>>`.
// Returns the string name of T (per `policy`), or nullopt if `qt` is neither.
// Handles both forms uniformly so callers don't need separate code paths.
std::optional<std::string>
unwrapPersistentType(clang::QualType qt,
                        const clang::PrintingPolicy& policy);

// Wrap a class-field's original type into its persistent equivalent and
// report any newly-discovered user-defined classes.
//   `U* field`          -> "pmem_ptr<persistent<U>>"  (and queues U)
//   `fundamental f`     -> "persistent<F>"
//   `Class f` (value)   -> "persistent<Class>"        (and queues Class)
//   `int* p`            -> "persistent<int*>"         (library primitive spec)
//   anything else       -> original type with " /* unwrapped */" suffix
//
// `typesToSpecialize` is the seen-set; `workList` is the BFS queue. A newly
// referenced user-defined class is inserted into both, simultaneously.
std::string
wrapFieldType(clang::QualType qt,
                const clang::PrintingPolicy& policy,
                std::set<std::string>& typesToSpecialize,
                std::vector<std::string>& workList);

// Flush every modified file in the Rewriter back to disk. Iterates the
// rewriter's per-FileID buffers (not just the main file) so passes that
// rewrite headers (e.g. the recursive specializer adding `persistent<T>`
// into Stack.hpp) land on disk along with the main translation unit.
void writeAllRewrites(clang::Rewriter& rewriter, clang::ASTContext& context);

}

#endif
