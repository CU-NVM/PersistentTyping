#include <clang/Driver/Options.h>
#include <clang/Tooling/CompilationDatabase.h>
#include "clang/Tooling/Tooling.h"
#include <llvm/ADT/StringSwitch.h>
#include <llvm/Option/ArgList.h>
#include <llvm/Option/OptTable.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include "llvm/Support/raw_ostream.h"
#include "actions/recurseFrontendAction.h"
#include "actions/rootSetupFrontendAction.h"
#include "transformer/rootSetup.h"
// #include "actions/castFrontendAction.h"  // disabled: source not in build
#include <set>
#include <string>
#include <vector>

using namespace std;
using namespace llvm;
using namespace clang;
using namespace clang::tooling;

// Ordered sequence for --pass=all
static const llvm::StringRef AllPasses[] = {"root-setup", "recurse-persistent"};

// Maps pass name to its frontend action factory. root-setup is not here: it
// needs a table shared by every TU, see runRootSetup.
static std::unique_ptr<FrontendActionFactory>
makeFactoryForPass(llvm::StringRef Name) {
  return llvm::StringSwitch<std::unique_ptr<FrontendActionFactory>>(Name)
    .Case("recurse-persistent", newFrontendActionFactory<RecurseFrontendAction>())
    // .Case("cast",    newFrontendActionFactory<CastFrontendAction>())  // disabled
    .Default(nullptr);
}

static void printUsage() {
    llvm::outs()
        << "usage: persist-tool [--pass=<name>] [--dump-roots] <clang++ arguments>\n\n"
        << "Takes the same arguments as clang++, e.g.\n"
        << "  persist-tool src/a.cpp src/b.cpp -Iinclude -std=c++20 -o prog\n\n"
        << "passes:\n"
        << "  all (default)\n"
        << "  root-setup\n"
        << "  recurse-persistent\n\n"
        << "--dump-roots  print the roots root-setup finds across all files and stop;\n"
        << "              nothing is rewritten\n";
}

// Collects the roots of every TU into one table, then emits the shared root
// header and rewrites the sources once, so a root can live in any file.
static int runRootSetup(const CompilationDatabase &Compilations,
                        const std::vector<std::string> &Files, bool DumpRoots) {
    RootTable Table;
    RootSetupActionFactory F(Table);
    ClangTool Tool(Compilations, Files);
    if (int ret = Tool.run(&F))
        return ret;
    if (DumpRoots) {
        Table.print(llvm::outs());
        return 0;
    }
    return emitRoots(Table) ? 0 : 1;
}

static bool isSourceFile(llvm::StringRef Path) {
    static const std::set<std::string> Exts = {".c", ".cc", ".cpp", ".cxx", ".c++", ".C"};
    return Exts.count(llvm::sys::path::extension(Path).str()) > 0;
}

int main(int argc, const char **argv) {
    // Pull out the tool's own options; everything else is a clang++ command line.
    std::string PassName = "all";
    bool DumpRoots = false;
    std::vector<const char*> ClangArgs;
    for (int i = 1; i < argc; ++i) {
        llvm::StringRef Arg = argv[i];
        if (Arg == "--help") {
            printUsage();
            return 0;
        } else if (Arg.consume_front("--pass=")) {
            PassName = Arg.str();
        } else if (Arg == "--pass") {
            if (i + 1 == argc) {
                llvm::errs() << "error: --pass needs a value\n";
                return 1;
            }
            PassName = argv[++i];
        } else if (Arg == "--dump-roots") {
            DumpRoots = true;
        } else if (Arg == "--") {
            continue;   // old `files -- flags` form: same split either way
        } else {
            ClangArgs.push_back(argv[i]);
        }
    }

    // Let clang's driver table decide which arguments are inputs, so a value
    // like the `x.cpp` in `-include x.cpp` is not mistaken for a source.
    const llvm::opt::OptTable &Opts = driver::getDriverOptTable();
    unsigned MissingIdx = 0, MissingCount = 0;
    llvm::opt::InputArgList Args = Opts.ParseArgs(ClangArgs, MissingIdx, MissingCount);
    if (MissingCount) {
        llvm::errs() << "error: missing value for '" << Args.getArgString(MissingIdx) << "'\n";
        return 1;
    }

    std::vector<std::string> Files;
    std::set<unsigned> InputIdx;
    for (const llvm::opt::Arg *A : Args.filtered(driver::options::OPT_INPUT)) {
        InputIdx.insert(A->getIndex());
        if (isSourceFile(A->getValue()))
            Files.push_back(A->getValue());
    }
    if (Files.empty()) {
        llvm::errs() << "error: no source files given\n";
        printUsage();
        return 1;
    }

    // Every non-input argument applies to every source, as with one clang++ call.
    std::vector<std::string> Flags;
    for (unsigned i = 0; i < ClangArgs.size(); ++i)
        if (!InputIdx.count(i))
            Flags.push_back(ClangArgs[i]);
    if (!Args.hasArg(driver::options::OPT_resource_dir))
        Flags.push_back("-resource-dir=" PERSIST_CLANG_RESOURCE_DIR);

    llvm::SmallString<256> Cwd;
    if (std::error_code EC = llvm::sys::fs::current_path(Cwd)) {
        llvm::errs() << "error: cannot get working directory: " << EC.message() << "\n";
        return 1;
    }
    FixedCompilationDatabase Compilations(Cwd, Flags);

    // --dump-roots only looks, so later passes would see unchanged sources
    if (DumpRoots || PassName == "root-setup")
        return runRootSetup(Compilations, Files, DumpRoots);

    // Run all passes sequentially, stop on first failure
    if (PassName == "all") {
        for (llvm::StringRef Pass : AllPasses) {
            if (Pass == "root-setup") {
                if (int ret = runRootSetup(Compilations, Files, false))
                    return ret;
                continue;
            }
            ClangTool Tool(Compilations, Files);   // fresh per pass
            auto F = makeFactoryForPass(Pass);
            if (int ret = Tool.run(F.get()))
                return ret;
        }
        return 0;
    }
    auto Factory = makeFactoryForPass(PassName);
    if (!Factory) {
        errs() << "error: unknown pass '" << PassName << "'\n";
        printUsage();
        return 1;
    }
    ClangTool Tool(Compilations, Files);
    return Tool.run(Factory.get());
}
