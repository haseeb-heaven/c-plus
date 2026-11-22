#ifndef CPLUS_OWNERSHIP_ANALYSIS_H
#define CPLUS_OWNERSHIP_ANALYSIS_H

#include "llvm/ADT/StringRef.h"

#include <clang/AST/Decl.h>

#include <functional>
#include <string>
#include <vector>

namespace cplus {

/// Minimal interprocedural effect summary for known library/user functions.
struct FunctionEffect {
  bool allocates = false;      ///< returns an owned allocation
  bool frees = false;          ///< frees a pointer argument
  bool borrows = false;        ///< observes a pointer argument without taking ownership
  bool writes = false;         ///< writes through a pointer/size argument
  bool returnsOwned = false;   ///< return value transfers ownership
  bool nullableArg = false;    ///< pointer argument may be NULL
  bool checkedSize = false;    ///< has an explicit destination-capacity argument
  bool mayFail = false;        ///< failure is reported in return value/errno
  int freedArg = -1;           ///< argument index freed, when known
  int destArg = -1;            ///< destination buffer argument, when known
  int sizeArg = -1;            ///< destination capacity argument, when known

  // --- Precise v2 argument-index metadata ---------------------------------
  // Additive: these carry the exact indices encoded in the v2 contract's
  // structured fields. The scalar fields above are deliberately left
  // untouched so existing diagnostics are unchanged; new consumers should
  // prefer these precise lists.
  /// Exact indices from the `nullableArgs` array (arguments accepting NULL).
  std::vector<int> nullableArgIndices;
  /// Argument indices denoted by `ownershipTransfer` when it refers to an
  /// argument (e.g. "arg", or the old pointer invalidated on success).
  /// A return-value transfer ("return") yields no index.
  std::vector<int> ownershipTransferArgs;
  /// Argument indices freed/consumed, derived from `freeEffects`.
  std::vector<int> freeArgs;
  /// Argument indices written through, derived from `writeEffects`.
  std::vector<int> writeArgs;

  std::string category = "CPLUS-SAFE-FFI"; ///< diagnostic category
};

/// Known libc effects. Unknown functions are NOT assumed safe.
FunctionEffect libcEffect(llvm::StringRef name);

/// A contract that could not be fully interpreted. STRICT safe mode must
/// reject calls to functions with such contracts instead of assuming safety.
struct ContractIssue {
  std::string function; ///< empty for file-level issues
  std::string message;
};

/// Issues found while loading/validating the machine-readable contracts.
const std::vector<ContractIssue> &contractIssues();

/// True when the contract file was found and parsed successfully.
bool contractsLoaded();

/// Report callback used by the CFG analyses.
using CfgReportFn =
    std::function<void(clang::SourceLocation, const std::string &)>;

/// Path-sensitive definite-initialization check over Clang's CFG.
void checkDefiniteInitialization(clang::ASTContext &ctx,
                                 clang::FunctionDecl *fd, const CfgReportFn &report);

} // namespace cplus

#endif
