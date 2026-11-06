// Table-driven test of utils::translateType (P).
//
// Every row's source type becomes `using case_N = <type>;` in one snippet,
// which is parsed with persistenttype.hpp; P runs on each alias. A row
// expects either a spelling plus the classes reached, or an error containing
// some text. Each spelling P produces is then parsed in turn and must map to
// itself: P(P(T)) = P(T).
//
// usage: translate-type-test [-v]

#include "../utils/utils.h"
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <cstring>
#include <map>

namespace {

struct Row {
    std::string type;
    std::string spelling;               // empty: an error is expected
    std::vector<std::string> classes;
    std::string error;                  // text the error must contain
};

const char* PRELUDE = R"(
#include "persistenttype.hpp"
#include <cstddef>
#include <string>
struct C { int x; C* next; };
enum Color { Red, Green };
namespace ns {
    struct D { int y; };
    enum class E { A, B };
    inline namespace v1 { struct I { int z; }; }
}
namespace { struct Anon { int a; }; }
struct Outer { struct Inner { int i; }; };
union U { int i; float f; };
struct V { virtual void f(); int v; };
struct Base { int b; };
struct Derived : Base { int d; };
template<class T> struct Box { T t; };
using CPtr = C*;
)";

const std::vector<Row> ROWS = {
    // fundamentals and enums
    {"int",                          "persistent<int>", {}, ""},
    {"bool",                         "persistent<bool>", {}, ""},
    {"unsigned long",                "persistent<unsigned long>", {}, ""},
    {"double",                       "persistent<double>", {}, ""},
    {"Color",                        "persistent<Color>", {}, ""},
    {"ns::E",                        "persistent<ns::E>", {}, ""},
    // classes
    {"C",                            "persistent<C>", {"C"}, ""},
    {"ns::D",                        "persistent<ns::D>", {"ns::D"}, ""},
    {"ns::I",                        "persistent<ns::I>", {"ns::I"}, ""},
    {"Anon",                         "persistent<Anon>", {"Anon"}, ""},
    {"Outer::Inner",                 "persistent<Outer::Inner>", {"Outer::Inner"}, ""},
    // pointers
    {"C*",                           "pmem_ptr<persistent<C>>", {"C"}, ""},
    {"C**",                          "pmem_ptr<pmem_ptr<persistent<C>>>", {"C"}, ""},
    {"CPtr",                         "pmem_ptr<persistent<C>>", {"C"}, ""},
    {"ns::D*",                       "pmem_ptr<persistent<ns::D>>", {"ns::D"}, ""},
    {"int*",                         "pmem_ptr<persistent<int>>", {}, ""},
    {"char*",                        "pmem_ptr<persistent<char>>", {}, ""},
    // const
    {"const int",                    "const persistent<int>", {}, ""},
    {"const char*",                  "pmem_ptr<const persistent<char>>", {}, ""},
    {"C* const",                     "const pmem_ptr<persistent<C>>", {"C"}, ""},
    // arrays
    {"int[4]",                       "persistent<int>[4]", {}, ""},
    {"int[2][3]",                    "persistent<int>[2][3]", {}, ""},
    {"C*[4]",                        "pmem_ptr<persistent<C>>[4]", {"C"}, ""},
    {"C[2]",                         "persistent<C>[2]", {"C"}, ""},
    // user-written wrappers are normalised
    {"persistent<C>",                "persistent<C>", {"C"}, ""},
    {"persistent<C*>",               "pmem_ptr<persistent<C>>", {"C"}, ""},
    {"persistent<persistent<C>*>",   "pmem_ptr<persistent<C>>", {"C"}, ""},
    {"pmem_ptr<persistent<C>>",      "pmem_ptr<persistent<C>>", {"C"}, ""},
    {"pmem_ptr<C>",                  "pmem_ptr<persistent<C>>", {"C"}, ""},
    {"persistent<persistent<int>>",  "persistent<int>", {}, ""},
    {"persistent<int*>",             "pmem_ptr<persistent<int>>", {}, ""},
    {"persistent<C>*[4]",            "pmem_ptr<persistent<C>>[4]", {"C"}, ""},
    {"persistent<C>**",              "pmem_ptr<pmem_ptr<persistent<C>>>", {"C"}, ""},
    // errors
    {"int&",                         "", {}, "references cannot live in pmem"},
    {"C&&",                          "", {}, "references cannot live in pmem"},
    {"U",                            "", {}, "unions cannot live in pmem"},
    {"U*",                           "", {}, "unions cannot live in pmem"},
    {"void*",                        "", {}, "void* has no pointee type"},
    {"void (*)()",                   "", {}, "function pointers cannot live in pmem"},
    {"std::string",                  "", {}, "library types cannot live in pmem"},
    {"std::string*",                 "", {}, "library types cannot live in pmem"},
    {"std::nullptr_t",               "", {}, "has no persistent form"},
    {"void",                         "", {}, "has no persistent form"},
    {"V",                            "", {}, "virtual methods"},
    {"V*",                           "", {}, "virtual methods"},
    {"Derived",                      "", {}, "inheritance"},
    {"Box<int>",                     "", {}, "class templates are not supported"},
    {"volatile int",                 "", {}, "volatile"},
    {"int C::*",                     "", {}, "pointers to members"},
    {"int (*)[4]",                   "", {}, "pointers to arrays"},
    {"int[]",                        "", {}, "constant size"},
    {"persistent<int&>",             "", {}, "references cannot live in pmem"},
    {"persistent<std::string>*",     "", {}, "library types cannot live in pmem"},
};

std::unique_ptr<clang::ASTUnit> parse(const std::vector<std::string>& types) {
    std::string code = PRELUDE;
    for (size_t i = 0; i < types.size(); i++)
        code += "using case_" + std::to_string(i) + " = " + types[i] + ";\n";
    std::vector<std::string> args = {
        "-std=c++17", "-I" PERSIST_LIB_DIR, "-resource-dir=" PERSIST_CLANG_RESOURCE_DIR,
    };
    return clang::tooling::buildASTFromCodeWithArgs(code, args, "cases.cc");
}

// case_N -> its aliased type
std::map<std::string, clang::QualType> aliases(clang::ASTUnit& ast) {
    std::map<std::string, clang::QualType> out;
    for (const auto* d : ast.getASTContext().getTranslationUnitDecl()->decls())
        if (const auto* td = llvm::dyn_cast<clang::TypeAliasDecl>(d))
            if (td->getName().starts_with("case_"))
                out[td->getNameAsString()] = td->getUnderlyingType();
    return out;
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ",") + x;
    return "{" + s + "}";
}

}   // namespace

int main(int argc, char** argv) {
    bool verbose = argc > 1 && !std::strcmp(argv[1], "-v");

    std::vector<std::string> types;
    for (const Row& r : ROWS) types.push_back(r.type);
    auto ast = parse(types);
    if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
        llvm::errs() << "error: the test snippet does not compile\n";
        return 1;
    }
    clang::PrintingPolicy policy = ast->getASTContext().getPrintingPolicy();
    auto cases = aliases(*ast);

    int failed = 0;
    std::vector<std::string> spellings;   // for the idempotence pass
    for (size_t i = 0; i < ROWS.size(); i++) {
        const Row& r = ROWS[i];
        utils::TypeTranslation tt = utils::translateType(cases["case_" + std::to_string(i)], policy);
        std::string got = tt.ok() ? tt.spelling() + " " + join(tt.classes) : "error " + tt.error;
        bool pass = r.spelling.empty()
            ? !tt.ok() && tt.error.find(r.error) != std::string::npos
            : tt.ok() && tt.spelling() == r.spelling && tt.classes == r.classes;
        if (tt.ok()) spellings.push_back(tt.spelling());
        if (!pass) failed++;
        if (!pass || verbose) {
            std::string want = r.spelling.empty() ? "error ..." + r.error + "..."
                                                  : r.spelling + " " + join(r.classes);
            llvm::outs() << (pass ? "PASS  " : "FAIL  ") << r.type << "\n"
                         << "        got:  " << got << "\n";
            if (!pass) llvm::outs() << "        want: " << want << "\n";
        }
    }

    // P(P(T)) = P(T): every spelling P produced, parsed again, maps to itself.
    auto again = parse(spellings);
    if (!again || again->getDiagnostics().hasErrorOccurred()) {
        llvm::errs() << "error: P's own spellings do not compile\n";
        return 1;
    }
    auto twice = aliases(*again);
    for (size_t i = 0; i < spellings.size(); i++) {
        utils::TypeTranslation tt = utils::translateType(twice["case_" + std::to_string(i)],
                                                         again->getASTContext().getPrintingPolicy());
        bool pass = tt.ok() && tt.spelling() == spellings[i];
        if (!pass) failed++;
        if (!pass || verbose)
            llvm::outs() << (pass ? "PASS  " : "FAIL  ") << "P(" << spellings[i] << ") = "
                         << (tt.ok() ? tt.spelling() : "error " + tt.error) << "\n";
    }

    size_t total = ROWS.size() + spellings.size();
    llvm::outs() << "\n" << total - failed << " PASS, " << failed << " FAIL"
                 << "  (" << ROWS.size() << " rows, " << spellings.size() << " idempotence checks)\n";
    return failed ? 1 : 0;
}
