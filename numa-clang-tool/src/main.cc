#include <llvm/Support/CommandLine.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"
#include "actions/recurseFrontendAction.h"
#include "actions/castFrontendAction.h"
#include <string>

using namespace std;
using namespace llvm;
using namespace clang;
using namespace clang::tooling;
using namespace llvm::cl;

static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// Ordered sequence for --pass=all
static const llvm::StringRef AllPasses[] = {"recurse", "cast"};

// Maps pass name to its frontend action factory
static std::unique_ptr<FrontendActionFactory>
makeFactoryForPass(llvm::StringRef Name) {
  return llvm::StringSwitch<std::unique_ptr<FrontendActionFactory>>(Name)
    .Case("recurse", newFrontendActionFactory<RecurseFrontendAction>())
    .Case("cast",    newFrontendActionFactory<CastFrontendAction>())
    .Default(nullptr);
}

int main(int argc, const char **argv) {
    static cl::OptionCategory ToolCat("PersistentTyping-clang-tool options");

    static cl::opt<std::string> PassName(
        "pass",
        cl::desc("Which pass to run (e.g., recurse, cast)"),
        cl::value_desc("name"),
        cl::Required,
        cl::cat(ToolCat));

    static cl::list<std::string> InputFilesOpt(
        "input",
        cl::desc("Comma-separated or repeatable list of input files"),
        cl::ZeroOrMore,
        cl::CommaSeparated,
        cl::value_desc("file1.cpp,file2.cpp,..."),
        cl::cat(ToolCat));

    static cl::list<std::string> PositionalFiles(
        cl::Positional,
        cl::desc("<source files>"),
        cl::ZeroOrMore,
        cl::cat(ToolCat));

    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCat);
    if (!ExpectedParser) {
        errs() << toString(ExpectedParser.takeError()) << "\n";
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    std::vector<std::string> Files;
    if (!InputFilesOpt.empty()) {
        Files.insert(Files.end(), InputFilesOpt.begin(), InputFilesOpt.end());
    } else {
        auto Pos = OptionsParser.getSourcePathList();
        Files.insert(Files.end(), Pos.begin(), Pos.end());
    }

    if (Files.empty()) {
        llvm::errs() << "warning: no --input or positional file; using dummy file to satisfy parser.\n";
        auto DBFiles = OptionsParser.getSourcePathList();
        Files.push_back(DBFiles.empty() ? "dummy.cpp" : DBFiles.front());
    }

    ClangTool Tool(OptionsParser.getCompilations(), Files);

    // Run all passes sequentially, stop on first failure
    if (PassName == "all") {
        for (llvm::StringRef Pass : AllPasses) {
            auto F = makeFactoryForPass(Pass);
            if (int ret = Tool.run(F.get()))
                return ret;
        }
        return 0;
    }

    auto Factory = makeFactoryForPass(PassName);
    if (!Factory) {
        errs() << "error: unknown pass '" << PassName << "'. Available passes:\n"
               << "  - recurse\n"
               << "  - cast\n"
               << "  - all\n";
        return 1;
    }

    return Tool.run(Factory.get());
}
