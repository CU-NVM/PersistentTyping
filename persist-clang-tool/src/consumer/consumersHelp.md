# What is an ASTConsumer?

An `ASTConsumer` is the class you override to receive and act on a fully parsed AST. By the time Clang constructs your consumer and calls into it, the source file has already been completely lexed, preprocessed, and parsed — you never deal with raw tokens or `#include` directives. You only ever see the finished tree.

Clang hands your consumer the `ASTContext` — the root object that contains the entire AST, the type system, identifier tables, and the source manager. Everything about the parsed file is reachable from there.

---

## The key method: HandleTranslationUnit

```cpp
void RecurseConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    //...
}
```

A translation unit is the unit of compilation — one `.cc` file and everything it pulls in via `#include`. `HandleTranslationUnit` is called exactly once per file, after the entire translation unit has been parsed. This is where you kick off your analysis or transformation.

In this tool it does three things:
1. Constructs a `RecursivePersistentyper` and runs it — this walks the AST and records edits into `recurseRewriter`
2. Collects all file names seen by the rewriter
3. Calls `WriteOutput` to flush the recorded edits to disk

---

## The Rewriter

`recurseRewriter` is what makes this a source-to-source tool rather than just an analyzer. It does not modify the AST — it schedules text edits (insertions, replacements, deletions) against the original source buffer using `SourceLocation` values taken from AST nodes. When `WriteOutput` is called, those edits are flushed to the output files.

The rewriter is initialized in the constructor from the `CompilerInstance`:

```cpp
RecurseConsumer::RecurseConsumer(clang::CompilerInstance &compiler) {
    recurseRewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
}
```

It must be bound to the `SourceManager` before any edits can be made, because every edit is expressed as a source location — a file ID plus an offset into that file's buffer.

---

## WriteOutput

```cpp
void RecurseConsumer::WriteOutput(clang::SourceManager &SM)
```

Iterates over every file the source manager knows about (the `.cc` file and all its included headers), checks whether the rewriter has a modified buffer for that file, and if so writes the result to the corresponding output path. Files from system headers and internal library headers are skipped by name to avoid accidentally rewriting things you don't own.

---

## Lifetime

The consumer is created by the action's `CreateASTConsumer`, owned by Clang for the duration of the file's compilation, and destroyed when Clang moves on to the next file. This means:
- Each file gets a fresh consumer with its own `recurseRewriter`
- No state carries over between files
- All output must be written before `HandleTranslationUnit` returns, since that is the last time your code runs for that file
