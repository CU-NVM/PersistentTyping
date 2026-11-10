# What is an ASTConsumer?

An `ASTConsumer` is the class you override to receive and act on a fully parsed AST. By the time Clang constructs your consumer and calls into it, the source file has already been completely lexed, preprocessed, and parsed — you never deal with raw tokens or `#include` directives. You only ever see the finished tree.

Clang hands your consumer the `ASTContext` — the root object that contains the entire AST, the type system, identifier tables, and the source manager. Everything about the parsed file is reachable from there.

---

## The key method: HandleTranslationUnit

```cpp
void RecursePersistentConsumer::HandleTranslationUnit(clang::ASTContext &context) {
    //...
}
```

A translation unit is the unit of compilation — one `.cc` file and everything it pulls in via `#include`. `HandleTranslationUnit` is called exactly once per file, after the entire translation unit has been parsed. This is where you kick off your analysis or transformation.

In this tool it constructs a `SpecializationBuilder` and runs it. The builder walks the AST, records its edits into the consumer's `rewriter`, and at the end writes them to disk with `utils::writeAllRewrites` (unless it reported an error).

---

## The Rewriter

`rewriter` is what makes this a source-to-source tool rather than just an analyzer. It does not modify the AST — it schedules text edits (insertions, replacements, deletions) against the original source buffer using `SourceLocation` values taken from AST nodes. When `utils::writeAllRewrites` is called, those edits are flushed to the files.

The rewriter is initialized in the constructor from the `CompilerInstance`:

```cpp
RecursePersistentConsumer::RecursePersistentConsumer(clang::CompilerInstance &compiler) {
    rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
}
```

It must be bound to the `SourceManager` before any edits can be made, because every edit is expressed as a source location — a file ID plus an offset into that file's buffer.

---

## Writing the edits: utils::writeAllRewrites

```cpp
void writeAllRewrites(clang::Rewriter& rewriter, clang::ASTContext& context);
```

Iterates over every buffer the rewriter modified (the `.cc` file and any header the pass edited, e.g. a `persistent<Stack>` landing in `Stack.hpp`) and writes each one back over its file.

---

## Lifetime

The consumer is created by the action's `CreateASTConsumer`, owned by Clang for the duration of the file's compilation, and destroyed when Clang moves on to the next file. This means:
- Each file gets a fresh consumer with its own `rewriter`
- No state carries over between files
- All output must be written before `HandleTranslationUnit` returns, since that is the last time your code runs for that file
