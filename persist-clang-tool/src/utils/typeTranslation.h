#ifndef TYPETRANSLATION_H
#define TYPETRANSLATION_H

// The type translations of the closure doc: P(T), the type of something that
// lives in pmem (a field, an array element, a root), and L(T), the type of
// something that lives in DRAM but may point into pmem (a local, parameter or
// return value), plus the spellings generated code builds from them.

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <string>
#include <vector>

namespace utils {

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

// Builds a method signature string for the persistent<T> spec.
// Constructors are renamed to `persistent`, destructors to `~persistent`.
// Parameter and return types go through signatureType().
// Preserves static, const, parameter names, and default arguments (copied as
// written, not rewritten). Does NOT preserve `explicit`, `noexcept`,
// `constexpr`, attributes, ref-qualifiers.
std::string
buildMethodSignature(const clang::CXXMethodDecl* method,
                     const clang::PrintingPolicy& policy);

}

#endif
