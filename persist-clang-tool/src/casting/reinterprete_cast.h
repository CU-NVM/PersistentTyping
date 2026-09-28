#ifndef REINTERPRETE_CAST_H
#define REINTERPRETE_CAST_H

#include <clang/Rewrite/Core/Rewriter.h>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/Tooling.h"
#include <sstream>
#include <string>
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LLVMContext.h"




void numafyAndReinterpreteCast (clang::Rewriter &rewriter, clang::SourceManager &SM,const clang::LangOptions &LO, clang::CXXNewExpr* CXXNewExpr, llvm::APSInt nodeID);


#endif