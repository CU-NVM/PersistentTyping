# What is a Frontend Action?

A frontend action is Clang's plugin point — it is the mechanism by which you tell Clang what to *do* with a source file after it has been set up for compilation. Clang's frontend is responsible for everything from reading the source file off disk, running the preprocessor, parsing tokens into an AST, and then handing off to whatever comes next. Normally "what comes next" is code generation. A frontend action replaces that step with your own logic.

When you build a LibTooling tool, you are essentially running a stripped-down Clang compiler that, instead of emitting object code, calls your action. The `FrontendAction` base class defines the contract between Clang's driver and your code. There are several specializations — `ASTFrontendAction` is the one you care about for AST-based work because it guarantees Clang will fully parse the file into an AST before calling into you. Other specializations exist for operating at the preprocessor level (`PreprocessorFrontendAction`) or for generating code (`CodeGenAction`), but for source analysis and transformation, `ASTFrontendAction` is the right base.

Each source file gets its own fresh action instance, created by the `FrontendActionFactory` you pass to `ClangTool::run()`. This is why the factory pattern exists — `ClangTool` needs to stamp out one action per file.

---
# RecurseFrontendAction


# ASTFrontendAction vs ASTConsumer

## ASTFrontendAction — the per-file lifecycle manager

Exists at the **compiler level**. Clang creates one instance per file and uses it to manage the full compilation lifecycle: set up the compiler, parse the file, tear down. You override `CreateASTConsumer` to plug in your logic, but the action itself doesn't touch the AST — it only controls *when* things happen.

```cpp
std::unique_ptr<clang::ASTConsumer>
RecurseFrontendAction::CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    return std::make_unique<RecurseConsumer>(compiler);
}
```

The action's job is done the moment it returns the consumer. It has no AST knowledge — only access to `CompilerInstance` (source manager, language options, diagnostics).

**Available lifecycle hooks:**
- `BeginSourceFileAction()` — fires before parsing starts
- `EndSourceFileAction()` — fires after the consumer finishes (use this if the action should write output instead of the consumer)

---

## ASTConsumer — the AST-level worker

Exists at the **AST level**. By the time Clang calls into your consumer, the file has been fully lexed, preprocessed, and parsed. The consumer never sees tokens or `#include` directives — only the finished tree.

The key method is `HandleTranslationUnit`, which receives `ASTContext` — the root of the entire parsed tree:

```cpp
void RecurseConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    //...
}
```

This is where transformation logic lives. The consumer knows nothing about files or compiler flags — only the AST it was handed.

---

## Why they are split

| | `ASTFrontendAction` | `ASTConsumer` |
|---|---|---|
| Created by | `FrontendActionFactory` | Your action's `CreateASTConsumer` |
| Has access to | `CompilerInstance` (compiler machinery) | `ASTContext` (the parsed tree) |
| Called when | File begins/ends | After full parse completes |
| Knows about | Files, flags, preprocessor | Declarations, types, expressions |
| Your job here | Wiring and setup | Analysis and transformation |

The action is the **doorway** — it receives the compiler and hands you a configured worker. The consumer is the **worker** — it receives the finished AST and does the real job. Clang enforces this split because parsing must complete fully before any meaningful AST work can begin.
