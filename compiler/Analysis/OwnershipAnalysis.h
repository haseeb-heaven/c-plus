#ifndef CPLUS_OWNERSHIP_ANALYSIS_H
#define CPLUS_OWNERSHIP_ANALYSIS_H

#include "llvm/ADT/StringRef.h"

#include <clang/AST/Decl.h>

#include <functional>
#include <string>

namespace cplus {

/// Minimal interprocedural effect summary for known library/user functions.
struct FunctionEffect {
  bool allocates = false;      ///< returns an owned allocation
  bool frees = false;          ///< frees a pointer argument
  bool borrows = false;        ///< observes a pointer argument without taking ownership
  bool writes = false;         ///< writes through a pointer/size argument
  bool returnsOwned = false;   ///< return value transfers ownership
  int freedArg = -1;           ///< argument index freed, when known
};

/// Known libc effects. Unknown functions are NOT assumed safe.
FunctionEffect libcEffect(llvm::StringRef name);

/// Report callback used by the CFG analyses.
using CfgReportFn =
    std::function<void(clang::SourceLocation, const std::string &)>;

/// Path-sensitive definite-initialization check over Clang's CFG.
void checkDefiniteInitialization(clang::ASTContext &ctx,
                                 clang::FunctionDecl *fd, const CfgReportFn &report);

} // namespace cplus

#endif
